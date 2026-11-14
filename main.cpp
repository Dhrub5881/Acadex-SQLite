#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <sys/stat.h>
#include "sqlite3.h"
#include "dsa.h"

using namespace std;

const string DATA_DIR="data/";
const string STUDENTS_FILE=DATA_DIR+"students.txt"; // legacy import only
const string CRS_FILE=DATA_DIR+"crs.txt";           // legacy import only
const string NOTES_FILE=DATA_DIR+"notes.txt";       // legacy import only
const string PYQS_FILE=DATA_DIR+"pyqs.txt";         // legacy import only
const string NOTIFICATIONS_FILE=DATA_DIR+"notifications.txt"; // legacy import only
const char *DATABASE_FILE="acadex.db";
sqlite3 *g_db=nullptr;

string trim(string s){size_t a=s.find_first_not_of(" \t\r\n");if(a==string::npos)return "";size_t b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}
string lowerCopy(string s){transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return (char)tolower(c);});return s;}
string safeField(string s){replace(s.begin(),s.end(),'|','/');replace(s.begin(),s.end(),'\n',' ');replace(s.begin(),s.end(),'\r',' ');return trim(s);}
vector<string> split(const string&s,char d='|'){vector<string>v;string x;stringstream ss(s);while(getline(ss,x,d))v.push_back(x);return v;}
string ask(const string&p){cout<<p;string s;getline(cin,s);return trim(s);}
int askInt(const string&p,int lo,int hi){while(true){string s=ask(p);try{size_t idx=0;int n=stoi(s,&idx);if(idx==s.size()&&n>=lo&&n<=hi)return n;}catch(...){}cout<<"Enter a number from "<<lo<<" to "<<hi<<".\n";}}
string nowText(){time_t t=time(nullptr);tm *tmv=localtime(&t);if(!tmv)return "unknown time";char b[32];if(strftime(b,sizeof(b),"%Y-%m-%d %H:%M:%S",tmv)==0)return "unknown time";return b;}

// Demonstration-only password hash. Use Argon2id, scrypt, bcrypt or PBKDF2 for production.
string passwordHash(const string& pw,const string& salt){unsigned long long h=1469598103934665603ULL;for(unsigned char c:salt+":"+pw){h^=c;h*=1099511628211ULL;}ostringstream o;o<<hex<<h;return o.str();}

struct AcademicProfile{
    string course="B.Tech CSE", specialization, section;
    int admissionYear=0, year=1, semester=1;
};
struct StudentRecord{
    string id, hash, name, phone, email;
    AcademicProfile academic;
};
struct CRRecord{
    string id, hash, name, phone, email;
    AcademicProfile academic;
    bool profileComplete=false;
};
struct Material{
    string id,title,subject,unit,topic,path,course,specialization,section,uploader,uploadedAt;
    int semester=1;
};
class User {
protected:
    string id, name, role;
public:
    User(string i,string n,string r):id(i),name(n),role(r){}
    virtual ~User()=default;
    string getId()const{return id;}
    string getName()const{return name;}
    string getRole()const{return role;}
    virtual void showMenu()const=0;
};
class StudentUser:public User{
public:
    StudentUser(string i,string n):User(i,n,"Student"){}
    void showMenu()const override{
        cout<<"\n========== STUDENT DASHBOARD ==========\n1. Latest Notes for My Section\n2. Subject-Wise Notes (Subject -> Unit -> Notes)\n3. Previous-Year Questions (PYQs)\n4. Previous Semester Notes\n5. My Profile\n6. Notifications\n7. Logout\n";
    }
};
class CRUser:public User{
public:
    CRUser(string i,string n):User(i,n,"CR"){}
    void showMenu()const override{
        cout<<"\n========== CR DASHBOARD ==========\n1. View My Section Notes\n2. Upload Notes\n3. Delete My Section Notes\n4. Upload PYQ for My Section\n5. Delete My Section PYQ\n6. Other Sections' Notes (read-only)\n7. Notifications\n8. My Profile\n9. Logout\n";
    }
};
class AdminUser:public User{
public:
    AdminUser(string i,string n):User(i,n,"Admin"){}
    void showMenu()const override{
        cout<<"\n========== ADMIN DASHBOARD ==========\n1. Add CR Details and Password\n2. See Section Notes and Edit/Delete\n3. View All CR and Student Details by Specialization/Section\n4. Change CR Login ID/Password\n5. Remove CR Account\n6. Check/Edit/Delete All Notes\n7. Check/Edit/Delete All PYQs\n8. View Notifications\n9. Logout\n";
    }
};

NotificationStack notificationStack;
NoteNode* dsaNotes=nullptr;

bool dbExec(const string &sql){
    char *err=nullptr;
    int rc=sqlite3_exec(g_db,sql.c_str(),nullptr,nullptr,&err);
    if(rc!=SQLITE_OK){cerr<<"Database error: "<<(err?err:sqlite3_errmsg(g_db))<<"\n";if(err)sqlite3_free(err);return false;}
    return true;
}
void bindText(sqlite3_stmt *st,int idx,const string &value){sqlite3_bind_text(st,idx,value.c_str(),-1,SQLITE_TRANSIENT);}
string colText(sqlite3_stmt *st,int col){const unsigned char *p=sqlite3_column_text(st,col);return p?(const char*)p:"";}
bool prepare(sqlite3_stmt **st,const char *sql){int rc=sqlite3_prepare_v2(g_db,sql,-1,st,nullptr);if(rc!=SQLITE_OK){cerr<<"Database prepare error: "<<sqlite3_errmsg(g_db)<<"\n";return false;}return true;}
string materialTable(const string &file){return file==PYQS_FILE?"pyqs":"notes";}
void ensureFiles(){
#ifdef _WIN32
    system("if not exist data mkdir data");system("if not exist study_material mkdir study_material");
#else
    mkdir("data",0755);mkdir("study_material",0755);
#endif
    int rc=sqlite3_open(DATABASE_FILE,&g_db);
    if(rc!=SQLITE_OK){cerr<<"Could not open SQLite database: "<<(g_db?sqlite3_errmsg(g_db):"unknown error")<<"\n";exit(1);}
    sqlite3_busy_timeout(g_db,5000);
    const char *schema=
      "PRAGMA foreign_keys=ON;"
      "CREATE TABLE IF NOT EXISTS students(id TEXT PRIMARY KEY COLLATE NOCASE,hash TEXT NOT NULL,name TEXT,phone TEXT,email TEXT,course TEXT,specialization TEXT,admission_year INTEGER,year INTEGER,semester INTEGER,section TEXT);"
      "CREATE TABLE IF NOT EXISTS crs(id TEXT PRIMARY KEY COLLATE NOCASE,hash TEXT NOT NULL,profile_complete INTEGER NOT NULL DEFAULT 0,name TEXT,phone TEXT,email TEXT,course TEXT,specialization TEXT,section TEXT);"
      "CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY,title TEXT,subject TEXT,unit TEXT,topic TEXT,path TEXT,course TEXT,specialization TEXT,section TEXT,semester INTEGER,uploader TEXT,uploaded_at TEXT);"
      "CREATE TABLE IF NOT EXISTS pyqs(id TEXT PRIMARY KEY,title TEXT,subject TEXT,unit TEXT,topic TEXT,path TEXT,course TEXT,specialization TEXT,section TEXT,semester INTEGER,uploader TEXT,uploaded_at TEXT);"
      "CREATE TABLE IF NOT EXISTS notifications(id INTEGER PRIMARY KEY AUTOINCREMENT,created_at TEXT NOT NULL,message TEXT NOT NULL);"
      "CREATE INDEX IF NOT EXISTS idx_notes_section ON notes(course,specialization,section,semester);"
      "CREATE INDEX IF NOT EXISTS idx_pyqs_section ON pyqs(course,specialization,section,semester);";
    if(!dbExec(schema))exit(1);
}
string sectionFor(const string&spec){
    if(spec=="AI/ML"){cout<<"Sections: ML1 ML2 ML3 ML4 ML5 ML6\n";return "ML"+to_string(askInt("Choose section number (ML1=1 ... ML6=6): ",1,6));}
    cout<<"Sections: A B C D E F G\n";return string(1,char('A'+askInt("Choose section number (A=1 ... G=7): ",1,7)-1));
}
vector<StudentRecord> loadStudents(){
    vector<StudentRecord> v;sqlite3_stmt *st=nullptr;
    if(!prepare(&st,"SELECT id,hash,name,phone,email,course,specialization,admission_year,year,semester,section FROM students ORDER BY rowid"))return v;
    while(sqlite3_step(st)==SQLITE_ROW){StudentRecord s;s.id=colText(st,0);s.hash=colText(st,1);s.name=colText(st,2);s.phone=colText(st,3);s.email=colText(st,4);s.academic.course=colText(st,5);s.academic.specialization=colText(st,6);s.academic.admissionYear=sqlite3_column_int(st,7);s.academic.year=sqlite3_column_int(st,8);s.academic.semester=sqlite3_column_int(st,9);s.academic.section=colText(st,10);v.push_back(s);}sqlite3_finalize(st);return v;
}
void saveStudent(const StudentRecord&s){
    sqlite3_stmt *st=nullptr;const char *sql="INSERT INTO students(id,hash,name,phone,email,course,specialization,admission_year,year,semester,section) VALUES(?,?,?,?,?,?,?,?,?,?,?)";
    if(!prepare(&st,sql))return;bindText(st,1,s.id);bindText(st,2,s.hash);bindText(st,3,s.name);bindText(st,4,s.phone);bindText(st,5,s.email);bindText(st,6,s.academic.course);bindText(st,7,s.academic.specialization);sqlite3_bind_int(st,8,s.academic.admissionYear);sqlite3_bind_int(st,9,s.academic.year);sqlite3_bind_int(st,10,s.academic.semester);bindText(st,11,s.academic.section);
    if(sqlite3_step(st)!=SQLITE_DONE)cerr<<"Could not save student: "<<sqlite3_errmsg(g_db)<<"\n";sqlite3_finalize(st);
}
vector<CRRecord> loadCRs(){
    vector<CRRecord>v;sqlite3_stmt *st=nullptr;if(!prepare(&st,"SELECT id,hash,profile_complete,name,phone,email,course,specialization,section FROM crs ORDER BY rowid"))return v;
    while(sqlite3_step(st)==SQLITE_ROW){CRRecord c;c.id=colText(st,0);c.hash=colText(st,1);c.profileComplete=sqlite3_column_int(st,2)!=0;c.name=colText(st,3);c.phone=colText(st,4);c.email=colText(st,5);c.academic.course=colText(st,6);c.academic.specialization=colText(st,7);c.academic.section=colText(st,8);v.push_back(c);}sqlite3_finalize(st);return v;
}
void saveCRs(const vector<CRRecord>&v){
    if(!dbExec("BEGIN IMMEDIATE"))return;if(!dbExec("DELETE FROM crs")){dbExec("ROLLBACK");return;}
    sqlite3_stmt *st=nullptr;if(!prepare(&st,"INSERT INTO crs(id,hash,profile_complete,name,phone,email,course,specialization,section) VALUES(?,?,?,?,?,?,?,?,?)")){dbExec("ROLLBACK");return;}
    bool ok=true;for(const auto&c:v){sqlite3_reset(st);sqlite3_clear_bindings(st);bindText(st,1,c.id);bindText(st,2,c.hash);sqlite3_bind_int(st,3,c.profileComplete?1:0);bindText(st,4,c.name);bindText(st,5,c.phone);bindText(st,6,c.email);bindText(st,7,c.academic.course);bindText(st,8,c.academic.specialization);bindText(st,9,c.academic.section);if(sqlite3_step(st)!=SQLITE_DONE){ok=false;cerr<<"Could not save CR: "<<sqlite3_errmsg(g_db)<<"\n";break;}}
    sqlite3_finalize(st);dbExec(ok?"COMMIT":"ROLLBACK");
}
vector<Material> loadMaterials(const string&file){
    vector<Material>v;string sql="SELECT id,title,subject,unit,topic,path,course,specialization,section,semester,uploader,uploaded_at FROM "+materialTable(file)+" ORDER BY rowid";sqlite3_stmt *st=nullptr;if(!prepare(&st,sql.c_str()))return v;
    while(sqlite3_step(st)==SQLITE_ROW){Material m;m.id=colText(st,0);m.title=colText(st,1);m.subject=colText(st,2);m.unit=colText(st,3);m.topic=colText(st,4);m.path=colText(st,5);m.course=colText(st,6);m.specialization=colText(st,7);m.section=colText(st,8);m.semester=sqlite3_column_int(st,9);m.uploader=colText(st,10);m.uploadedAt=colText(st,11);v.push_back(m);}sqlite3_finalize(st);return v;
}
void saveMaterials(const string&file,const vector<Material>&v){
    string table=materialTable(file);if(!dbExec("BEGIN IMMEDIATE"))return;if(!dbExec("DELETE FROM "+table)){dbExec("ROLLBACK");return;}sqlite3_stmt *st=nullptr;string sql="INSERT INTO "+table+"(id,title,subject,unit,topic,path,course,specialization,section,semester,uploader,uploaded_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)";
    if(!prepare(&st,sql.c_str())){dbExec("ROLLBACK");return;}bool ok=true;for(const auto&m:v){sqlite3_reset(st);sqlite3_clear_bindings(st);bindText(st,1,m.id);bindText(st,2,m.title);bindText(st,3,m.subject);bindText(st,4,m.unit);bindText(st,5,m.topic);bindText(st,6,m.path);bindText(st,7,m.course);bindText(st,8,m.specialization);bindText(st,9,m.section);sqlite3_bind_int(st,10,m.semester);bindText(st,11,m.uploader);bindText(st,12,m.uploadedAt);if(sqlite3_step(st)!=SQLITE_DONE){ok=false;cerr<<"Could not save material: "<<sqlite3_errmsg(g_db)<<"\n";break;}}
    sqlite3_finalize(st);dbExec(ok?"COMMIT":"ROLLBACK");
}
int tableCount(const char *table){string sql="SELECT COUNT(*) FROM "+string(table);sqlite3_stmt *st=nullptr;if(!prepare(&st,sql.c_str()))return 0;int n=0;if(sqlite3_step(st)==SQLITE_ROW)n=sqlite3_column_int(st,0);sqlite3_finalize(st);return n;}
void migrateLegacyData(){
    // Import old text records once, only when the corresponding SQLite table is empty.
    string line;
    if(tableCount("students")==0){ifstream f(STUDENTS_FILE);while(getline(f,line)){auto a=split(line);if(a.size()<11)continue;StudentRecord s;s.id=a[0];s.hash=a[1];s.name=a[2];s.phone=a[3];s.email=a[4];s.academic.course=a[5];s.academic.specialization=a[6];s.academic.admissionYear=atoi(a[7].c_str());s.academic.year=atoi(a[8].c_str());s.academic.semester=atoi(a[9].c_str());s.academic.section=a[10];saveStudent(s);}}
    if(tableCount("crs")==0){ifstream f(CRS_FILE);while(getline(f,line)){auto a=split(line);if(a.size()<9)continue;CRRecord c;c.id=a[0];c.hash=a[1];c.profileComplete=a[2]=="1";c.name=a[3];c.phone=a[4];c.email=a[5];c.academic.course=a[6];c.academic.specialization=a[7];c.academic.section=a[8];auto all=loadCRs();all.push_back(c);saveCRs(all);}}
    for(string file:{NOTES_FILE,PYQS_FILE}){string table=materialTable(file);if(tableCount(table.c_str())==0){ifstream f(file);while(getline(f,line)){auto a=split(line);if(a.size()<12)continue;Material m;m.id=a[0];m.title=a[1];m.subject=a[2];m.unit=a[3];m.topic=a[4];m.path=a[5];m.course=a[6];m.specialization=a[7];m.section=a[8];m.semester=atoi(a[9].c_str());m.uploader=a[10];m.uploadedAt=a[11];auto all=loadMaterials(file);all.push_back(m);saveMaterials(file,all);}}}
    if(tableCount("notifications")==0){ifstream f(NOTIFICATIONS_FILE);while(getline(f,line)){auto a=split(line);if(a.size()<2)continue;sqlite3_stmt *st=nullptr;if(prepare(&st,"INSERT INTO notifications(created_at,message) VALUES(?,?)")){bindText(st,1,a[0]);bindText(st,2,a[1]);sqlite3_step(st);sqlite3_finalize(st);}}}
}
void addNotification(const string&msg){
    stack_push(&notificationStack,msg.c_str());sqlite3_stmt *st=nullptr;if(prepare(&st,"INSERT INTO notifications(created_at,message) VALUES(?,?)")){bindText(st,1,nowText());bindText(st,2,msg);if(sqlite3_step(st)!=SQLITE_DONE)cerr<<"Could not save notification: "<<sqlite3_errmsg(g_db)<<"\n";sqlite3_finalize(st);}
}
void loadNotifications(){
    stack_init(&notificationStack);sqlite3_stmt *st=nullptr;if(!prepare(&st,"SELECT created_at,message FROM notifications ORDER BY id DESC LIMIT 20"))return;vector<string>msgs;while(sqlite3_step(st)==SQLITE_ROW)msgs.push_back(colText(st,0)+" - "+colText(st,1));sqlite3_finalize(st);for(auto it=msgs.rbegin();it!=msgs.rend();++it)stack_push(&notificationStack,it->c_str());
}
bool studentIdExists(const string&id){for(auto&s:loadStudents())if(lowerCopy(s.id)==lowerCopy(id))return true;return false;}
bool crIdExists(const string&id){for(auto&c:loadCRs())if(lowerCopy(c.id)==lowerCopy(id))return true;return false;}
void rebuildDsaNotes(){
    free_notes(dsaNotes);dsaNotes=nullptr;auto v=loadMaterials(NOTES_FILE);int id=1;
    for(auto&m:v){string content="PDF: "+m.path;dsaNotes=add_note(dsaNotes,id++,m.section.c_str(),m.subject.c_str(),m.title.c_str(),content.c_str());}
}
void registerStudent(){
    StudentRecord s;s.id=ask("Create Student ID: ");if(s.id.empty()||studentIdExists(s.id)||crIdExists(s.id)||lowerCopy(s.id)=="admin"){cout<<"ID empty or already used.\n";return;}
    string pw=ask("Create password (minimum 8 characters): ");if(pw.size()<8){cout<<"Password must be at least 8 characters.\n";return;}
    s.name=ask("Full name: ");s.phone=ask("Phone number: ");s.email=ask("Email address: ");s.academic.course="B.Tech CSE";
    cout<<"Specialization:\n1. CSE General\n2. AI/ML\n";s.academic.specialization=askInt("Choose: ",1,2)==1?"General":"AI/ML";
    s.academic.admissionYear=askInt("Admission year (e.g. 2025): ",2000,2100);s.academic.year=askInt("Current year (1-4): ",1,4);
    s.academic.semester=askInt("Current semester: ",(s.academic.year-1)*2+1,s.academic.year*2);s.academic.section=sectionFor(s.academic.specialization);
    s.hash=passwordHash(pw,s.id);saveStudent(s);cout<<"Student registered successfully. Login ID: "<<s.id<<"\n";
}
bool studentLogin(StudentRecord&out){string id=ask("Student ID: "),pw=ask("Password: ");for(auto&s:loadStudents())if(s.id==id&&s.hash==passwordHash(pw,s.id)){out=s;return true;}cout<<"Invalid Student ID or password.\n";return false;}
bool crLogin(CRRecord&out){string id=ask("CR Login ID (created by admin): "),pw=ask("Password: ");for(auto&c:loadCRs())if(c.id==id&&c.hash==passwordHash(pw,c.id)){out=c;return true;}cout<<"Invalid CR ID/password. Contact admin to create/reset your account.\n";return false;}
bool adminLogin(){cout<<"Demo admin login: admin / admin123\n";return ask("Admin ID: ")=="admin"&&ask("Admin password: ")=="admin123";}
void showMaterial(const Material&m,int n){cout<<n<<". "<<m.title<<" | "<<m.subject<<" | "<<m.unit;if(!m.topic.empty())cout<<" | "<<m.topic;cout<<"\n   "<<m.course<<" | "<<m.specialization<<" | Semester "<<m.semester<<" | Section "<<m.section<<" | Uploader "<<m.uploader<<" | "<<m.uploadedAt<<"\n   PDF: "<<m.path<<"\n";}
void openPdf(const string&path){{ifstream check(path.c_str(), ios::binary); if(!check.good()){cout<<"PDF not found: "<<path<<"\n";return;}}
#ifdef _WIN32
string cmd="start \"\" \""+path+"\"";
#elif __APPLE__
string cmd="open \""+path+"\"";
#else
string cmd="xdg-open \""+path+"\"";
#endif
if(system(cmd.c_str())!=0)cout<<"Could not open automatically. Path: "<<path<<"\n";}
void chooseAndOpen(vector<Material>v){
    if(v.empty()){cout<<"No matching documents found.\n";return;}
    sort(v.begin(),v.end(),[](const Material&a,const Material&b){return a.uploadedAt>b.uploadedAt;});
    for(size_t i=0;i<v.size();++i)showMaterial(v[i],(int)i+1);
    int n=askInt("Choose PDF to open (0 to go back): ",0,(int)v.size());if(n)openPdf(v[n-1].path);
}
bool academicMatch(const Material&m,const AcademicProfile&a,bool allSections=false){
    return (m.course=="ALL"||m.course==a.course)&&(m.specialization=="ALL"||m.specialization==a.specialization)&&
        (allSections||m.section=="ALL"||m.section==a.section);
}
void subjectUnitBrowse(vector<Material>v){
    if(v.empty()){cout<<"No notes found.\n";return;}
    vector<string>subjects;for(auto&m:v)if(find(subjects.begin(),subjects.end(),m.subject)==subjects.end())subjects.push_back(m.subject);sort(subjects.begin(),subjects.end());
    cout<<"\nSubjects:\n";for(size_t i=0;i<subjects.size();++i)cout<<i+1<<". "<<subjects[i]<<"\n";
    int si=askInt("Select subject (0 to cancel): ",0,(int)subjects.size());if(!si)return;
    vector<Material>sv;for(auto&m:v)if(m.subject==subjects[si-1])sv.push_back(m);
    vector<string>units;for(auto&m:sv)if(find(units.begin(),units.end(),m.unit)==units.end())units.push_back(m.unit);sort(units.begin(),units.end());
    cout<<"\nUnits:\n";for(size_t i=0;i<units.size();++i)cout<<i+1<<". "<<units[i]<<"\n";
    int ui=askInt("Select unit (0 to cancel): ",0,(int)units.size());if(!ui)return;
    vector<Material>uv;for(auto&m:sv)if(m.unit==units[ui-1])uv.push_back(m);chooseAndOpen(uv);
}
void studentDashboard(const StudentRecord&s){
    StudentUser user(s.id,s.name);
    while(true){user.showMenu();int c=askInt("Choice: ",1,7);auto notes=loadMaterials(NOTES_FILE);
        if(c==1){vector<Material>v;for(auto&m:notes)if(academicMatch(m,s.academic)&&m.semester==s.academic.semester)v.push_back(m);chooseAndOpen(v);}
        else if(c==2){vector<Material>v;for(auto&m:notes)if(academicMatch(m,s.academic)&&m.semester==s.academic.semester) v.push_back(m);subjectUnitBrowse(v);}
        else if(c==3){
            auto all=loadMaterials(PYQS_FILE);vector<Material>v;for(auto&m:all)if((m.course=="ALL"||m.course==s.academic.course)&&(m.specialization=="ALL"||m.specialization==s.academic.specialization))v.push_back(m);
            if(v.empty()){cout<<"No PYQs uploaded yet.\n";continue;}
            vector<string>subs;for(auto&m:v)if(find(subs.begin(),subs.end(),m.subject)==subs.end())subs.push_back(m.subject);sort(subs.begin(),subs.end());
            for(size_t i=0;i<subs.size();++i)cout<<i+1<<". "<<subs[i]<<"\n";int si=askInt("Subject (0 cancel): ",0,(int)subs.size());if(!si)continue;
            vector<Material>sv;for(auto&m:v)if(m.subject==subs[si-1])sv.push_back(m);
            cout<<"1. Mid-Sem\n2. End-Sem\n";int ex=askInt("Exam type: ",1,2);string k=ex==1?"mid":"end";vector<Material>ev;for(auto&m:sv)if(lowerCopy(m.unit).find(k)!=string::npos)ev.push_back(m);
            vector<string>years;for(auto&m:ev)if(find(years.begin(),years.end(),m.topic)==years.end())years.push_back(m.topic);sort(years.begin(),years.end(),greater<string>());
            if(years.empty()){cout<<"No papers for that exam type.\n";continue;}for(size_t i=0;i<years.size();++i)cout<<i+1<<". "<<years[i]<<"\n";
            int yi=askInt("Year (0 cancel): ",0,(int)years.size());if(!yi)continue;vector<Material>chosen;for(auto&m:ev)if(m.topic==years[yi-1])chosen.push_back(m);chooseAndOpen(chosen);
        }else if(c==4){vector<Material>v;for(auto&m:notes)if(academicMatch(m,s.academic)&&m.semester<s.academic.semester)v.push_back(m);subjectUnitBrowse(v);}
        else if(c==5)cout<<"Name: "<<s.name<<"\nStudent ID: "<<s.id<<"\nPhone: "<<s.phone<<"\nEmail: "<<s.email<<"\nCourse: "<<s.academic.course<<"\nSpecialization: "<<s.academic.specialization<<"\nYear: "<<s.academic.year<<"\nSemester: "<<s.academic.semester<<"\nSection: "<<s.academic.section<<"\n";
        else if(c==6)stack_print_latest_first(&notificationStack);else break;
    }
}
void firstCRSetup(CRRecord&c){
    if(c.profileComplete)return;
    cout<<"\nFirst login: complete your CR profile. This is required once.\n";
    c.name=ask("Full name: ");c.phone=ask("Phone number: ");c.email=ask("Email address: ");c.profileComplete=true;
    auto all=loadCRs();for(auto&x:all)if(x.id==c.id)x=c;saveCRs(all);
    cout<<"CR profile saved.\n";
}
string copyPdfToStorage(const string &source,const string &id){
    ifstream in(source.c_str(),ios::binary);if(!in.good())return "";
    size_t pos=source.find_last_of("/\\\\");string name=pos==string::npos?source:source.substr(pos+1);if(name.empty())name="document.pdf";
    for(char &ch:name){unsigned char u=(unsigned char)ch;if(!(isalnum(u)||ch=='.'||ch=='_'||ch=='-'))ch='_';}
    string dest="study_material/"+id+"_"+name;ofstream out(dest.c_str(),ios::binary|ios::trunc);if(!out.good())return "";
    out<<in.rdbuf();out.close();if(!out.good())return "";return dest;
}
void addMaterialCR(CRRecord&c,bool pyq){
    Material m;m.id=(pyq?"PYQ":"NOTE")+to_string(time(nullptr))+to_string(rand()%9999);m.title=ask("Title: ");m.subject=ask("Subject: ");
    if(pyq){cout<<"1. Mid-Sem\n2. End-Sem\n";m.unit=askInt("Exam type: ",1,2)==1?"Mid-Sem":"End-Sem";m.topic=ask("Paper year (e.g. 2025): ");}
    else{m.unit=ask("Unit (e.g. Unit 1): ");m.topic=ask("Topic (e.g. Arrays): ");}
    string sourcePath=ask("PDF path (full path to an existing PDF): ");{ifstream check(sourcePath.c_str(), ios::binary); if(!check.good()){cout<<"File does not exist. Record not saved.\n";return;}}
    m.course=c.academic.course;m.specialization=c.academic.specialization;m.section=c.academic.section;m.semester=askInt("Semester (1-8): ",1,8);m.uploader=c.id;m.uploadedAt=nowText();
    m.path=copyPdfToStorage(sourcePath,m.id);if(m.path.empty()){cout<<"Could not copy PDF into study_material. Record not saved.\n";return;}
    string file=pyq?PYQS_FILE:NOTES_FILE;auto v=loadMaterials(file);v.push_back(m);saveMaterials(file,v);
    if(!pyq){string content="PDF: "+m.path;dsaNotes=add_note(dsaNotes,(int)v.size(),m.section.c_str(),m.subject.c_str(),m.title.c_str(),content.c_str());}
    addNotification(string(pyq?"New PYQ":"New notes")+" uploaded for "+m.specialization+" section "+m.section+": "+m.title);
    cout<<"Record saved for your assigned section "<<c.academic.section<<".\n";
}
void deleteCRMaterial(CRRecord&c,bool pyq){
    string file=pyq?PYQS_FILE:NOTES_FILE;auto v=loadMaterials(file);vector<int>idx;
    for(size_t i=0;i<v.size();++i)if(v[i].course==c.academic.course&&v[i].specialization==c.academic.specialization&&v[i].section==c.academic.section){showMaterial(v[i],(int)idx.size()+1);idx.push_back((int)i);}
    if(idx.empty()){cout<<"No records for your section.\n";return;}int n=askInt("Record to delete (0 cancel): ",0,(int)idx.size());if(!n)return;
    if(lowerCopy(ask("Type DELETE to confirm: "))!="delete"){cout<<"Cancelled.\n";return;}
    string title=v[idx[n-1]].title;v.erase(v.begin()+idx[n-1]);saveMaterials(file,v);rebuildDsaNotes();addNotification("CR "+c.id+" deleted "+(pyq?"PYQ: ":"notes: ")+title);cout<<"Catalogue record deleted. The PDF file itself was not deleted.\n";
}
void crDashboard(CRRecord c){
    firstCRSetup(c);CRUser user(c.id,c.name);
    while(true){user.showMenu();int ch=askInt("Choice: ",1,9);
        if(ch==1){vector<Material>v;for(auto&m:loadMaterials(NOTES_FILE))if(m.course==c.academic.course&&m.specialization==c.academic.specialization&&m.section==c.academic.section)v.push_back(m);subjectUnitBrowse(v);}
        else if(ch==2)addMaterialCR(c,false);
        else if(ch==3)deleteCRMaterial(c,false);
        else if(ch==4)addMaterialCR(c,true);
        else if(ch==5)deleteCRMaterial(c,true);
        else if(ch==6){vector<Material>v;for(auto&m:loadMaterials(NOTES_FILE))if(m.course==c.academic.course&&m.specialization==c.academic.specialization&&m.section!=c.academic.section)v.push_back(m);cout<<"Other sections: read-only. Upload/delete is disabled here.\n";chooseAndOpen(v);}
        else if(ch==7)stack_print_latest_first(&notificationStack);
        else if(ch==8)cout<<"Name: "<<c.name<<"\nCR ID: "<<c.id<<"\nPhone: "<<c.phone<<"\nEmail: "<<c.email<<"\nSpecialization: "<<c.academic.specialization<<"\nSection: "<<c.academic.section<<"\n";
        else break;
    }
}
void createCR(){
    CRRecord c;c.id=ask("Create CR Login ID: ");if(c.id.empty()||crIdExists(c.id)||studentIdExists(c.id)||lowerCopy(c.id)=="admin"){cout<<"ID empty or already used.\n";return;}
    string pw=ask("Set temporary password (minimum 8 characters): ");if(pw.size()<8){cout<<"Password must be at least 8 characters.\n";return;}
    c.academic.course="B.Tech CSE";cout<<"Specialization:\n1. CSE General\n2. AI/ML\n";c.academic.specialization=askInt("Choose: ",1,2)==1?"General":"AI/ML";c.academic.section=sectionFor(c.academic.specialization);c.hash=passwordHash(pw,c.id);c.profileComplete=false;
    auto all=loadCRs();all.push_back(c);saveCRs(all);cout<<"CR account created for "<<c.academic.specialization<<" Section "<<c.academic.section<<". Share the ID and temporary password privately.\n";
}
void crDirectoryAndStudents(){
    auto crs=loadCRs(); auto ss=loadStudents();
    vector<pair<string,vector<string>>> groups;
    for(string sec: {"A","B","C","D","E","F","G","ML1","ML2","ML3","ML4","ML5","ML6"}) {
        string spec=(sec.rfind("ML",0)==0)?"AI/ML":"General";
        vector<string> rows;
        for(auto&c:crs) if(c.academic.specialization==spec&&c.academic.section==sec)
            rows.push_back("CR: "+string(c.profileComplete?c.name:"[PROFILE PENDING]")+" | ID: "+c.id+" | Phone: "+(c.phone.empty()?"Not provided":c.phone)+" | Email: "+(c.email.empty()?"Not provided":c.email));
        for(auto&s:ss) if(s.academic.specialization==spec&&s.academic.section==sec)
            rows.push_back("Student: "+s.name+" | ID: "+s.id+" | Phone: "+s.phone+" | Email: "+s.email+" | Year "+to_string(s.academic.year)+" | Sem "+to_string(s.academic.semester));
        cout<<"\n========== "<<(spec=="AI/ML"?"AI/ML":"CSE GENERAL")<<" | SECTION "<<sec<<" ==========\n";
        if(rows.empty()) cout<<"No CR or students registered for this section.\n";
        else for(auto&row:rows) cout<<row<<"\n";
    }
    cout<<"\nPasswords are never displayed. Admin can reset CR passwords from option 4.\n";
}
void changeCRCredentials(){
    auto all=loadCRs();if(all.empty()){cout<<"No CR accounts.\n";return;}
    for(size_t i=0;i<all.size();++i)cout<<i+1<<". "<<all[i].id<<" | "<<all[i].academic.specialization<<" | "<<all[i].academic.section<<"\n";
    int n=askInt("Select CR (0 cancel): ",0,(int)all.size());if(!n)return;
    cout<<"1. Change login ID\n2. Reset password\n3. Both\n";int op=askInt("Choose: ",1,3);
    if(op==1||op==3){string id=ask("New CR ID: ");bool clash=id.empty()||lowerCopy(id)=="admin"||studentIdExists(id);for(size_t i=0;i<all.size();++i)if(i!=(size_t)n-1&&lowerCopy(all[i].id)==lowerCopy(id))clash=true;
        if(clash)cout<<"ID unavailable; keeping old ID.\n";else{string old=all[n-1].id;all[n-1].id=id;/* rehash requires the password, so force a reset after ID change */cout<<"ID changed. Set a new password now to re-hash credentials.\n";string pw=ask("New password (minimum 8 characters): ");if(pw.size()>=8)all[n-1].hash=passwordHash(pw,id);else{cout<<"Password too short; account password reset to unusable placeholder. Set a valid password immediately.\n";all[n-1].hash="RESET_REQUIRED";}}}
    if(op==2){string pw=ask("New password (minimum 8 characters): ");if(pw.size()<8)cout<<"Password too short; unchanged.\n";else all[n-1].hash=passwordHash(pw,all[n-1].id);}
    saveCRs(all);cout<<"CR account updated. Passwords are stored as hashes and cannot be displayed in plaintext.\n";
}
void removeCR(){
    auto all=loadCRs();if(all.empty()){cout<<"No CR accounts.\n";return;}for(size_t i=0;i<all.size();++i)cout<<i+1<<". "<<all[i].id<<" | "<<all[i].academic.specialization<<" | "<<all[i].academic.section<<"\n";
    int n=askInt("Select CR to remove (0 cancel): ",0,(int)all.size());if(!n)return;if(lowerCopy(ask("Type DELETE to confirm: "))!="delete"){cout<<"Cancelled.\n";return;}
    all.erase(all.begin()+n-1);saveCRs(all);cout<<"CR account removed. Existing notes remain in the catalogue.\n";
}
void adminManageMaterials(const string&file){
    auto v=loadMaterials(file);if(v.empty()){cout<<"No records.\n";return;}for(size_t i=0;i<v.size();++i)showMaterial(v[i],(int)i+1);
    cout<<"1. Delete record\n2. Edit title or PDF path\n3. Back\n";int op=askInt("Choose: ",1,3);if(op==3)return;int n=askInt("Record number: ",1,(int)v.size());
    if(op==1){if(lowerCopy(ask("Type DELETE to confirm: "))!="delete"){cout<<"Cancelled.\n";return;}string title=v[n-1].title;v.erase(v.begin()+n-1);saveMaterials(file,v);rebuildDsaNotes();addNotification("Admin deleted material: "+title);}
    else{v[n-1].title=ask("New title: ");string p=ask("New PDF path (blank keeps current): ");if(!p.empty()){{ifstream check(p.c_str(), ios::binary); if(check.good())v[n-1].path=p;else cout<<"Path not found; keeping existing path.\n";}}saveMaterials(file,v);rebuildDsaNotes();}
    cout<<"Changes saved.\n";
}
void adminManageSectionNotes(){
    cout<<"Specialization:\n1. CSE General\n2. AI/ML\n";
    string spec=askInt("Choose: ",1,2)==1?"General":"AI/ML";
    string sec=sectionFor(spec);
    auto all=loadMaterials(NOTES_FILE);vector<int>idx;
    for(size_t i=0;i<all.size();++i)if(all[i].specialization==spec&&all[i].section==sec){
        showMaterial(all[i],(int)idx.size()+1);idx.push_back((int)i);
    }
    if(idx.empty()){cout<<"No notes registered for "<<spec<<" section "<<sec<<".\n";return;}
    cout<<"1. Delete note record\n2. Edit note title/PDF path\n3. Back\n";
    int op=askInt("Choose: ",1,3);if(op==3)return;
    int n=askInt("Record number: ",1,(int)idx.size());int actual=idx[n-1];
    if(op==1){
        if(lowerCopy(ask("Type DELETE to confirm: "))!="delete"){cout<<"Cancelled.\n";return;}
        string title=all[actual].title;all.erase(all.begin()+actual);saveMaterials(NOTES_FILE,all);rebuildDsaNotes();addNotification("Admin deleted note: "+title);
    } else {
        all[actual].title=ask("New title: ");string path=ask("New PDF path (blank keeps current): ");
        if(!path.empty()){ifstream check(path.c_str(), ios::binary); if(check.good())all[actual].path=path;else cout<<"Path not found; keeping existing path.\n";}
        saveMaterials(NOTES_FILE,all);rebuildDsaNotes();addNotification("Admin edited note: "+all[actual].title);
    }
    cout<<"Section note changes saved.\n";
}

void adminDashboard(){
    AdminUser user("admin","System Admin");
    while(true){user.showMenu();int c=askInt("Choice: ",1,9);
        if(c==1)createCR();
        else if(c==2)adminManageSectionNotes();
        else if(c==3)crDirectoryAndStudents();
        else if(c==4)changeCRCredentials();
        else if(c==5)removeCR();
        else if(c==6)adminManageMaterials(NOTES_FILE);
        else if(c==7)adminManageMaterials(PYQS_FILE);
        else if(c==8)stack_print_latest_first(&notificationStack);
        else break;
    }
}
int main(){
    ensureFiles();migrateLegacyData();loadNotifications();rebuildDsaNotes();srand((unsigned)time(nullptr));
    cout<<"========================================\n ACADEX - TERMINAL NOTE MANAGEMENT\n========================================\n";
    while(true){
        cout<<"\n1. Student Registration\n2. Student Login\n3. CR Login\n4. Admin Login\n5. Exit\n";
        int ch=askInt("Enter choice: ",1,5);
        if(ch==1)registerStudent();
        else if(ch==2){StudentRecord s;if(studentLogin(s)){StudentUser u(s.id,s.name);cout<<"Welcome, "<<u.getName()<<".\n";studentDashboard(s);}}
        else if(ch==3){CRRecord c;if(crLogin(c)){firstCRSetup(c);c=([&](){for(auto&x:loadCRs())if(x.id==c.id)return x;return c;})();crDashboard(c);}}
        else if(ch==4){if(adminLogin())adminDashboard();else cout<<"Invalid admin credentials.\n";}
        else break;
    }
    free_notes(dsaNotes);
    if(g_db){sqlite3_close(g_db);g_db=nullptr;}
    cout<<"Thanks for using Acadex.\n";return 0;
}
