ACADEX — SQLITE DATABASE EDITION
================================

This version keeps the existing C++ terminal menus and the C DSA module, and replaces
text-file storage for new records with a local SQLite database.

WHAT IS STORED WHERE?
---------------------
- acadex.db: student accounts, CR accounts, notes metadata, PYQ metadata, notifications.
- study_material/: actual PDF copies made when a CR uploads notes or PYQs.
- dsa.c / dsa.h: existing linked-list notes and notification stack used by the project.

CR PDF UPLOAD WORKFLOW
----------------------
1. The CR signs in and chooses Upload Notes or Upload PYQ.
2. The CR enters the path to an existing PDF on this computer.
3. Acadex copies the PDF into study_material/ with a generated filename.
4. Acadex inserts the title, subject, unit/exam type, topic/year, semester, section,
   uploader, upload time, and stored file path into acadex.db.
5. Students browse the matching section and open the stored PDF.

IMPORTANT: This is a LOCAL terminal application. The database and PDFs are stored on the
computer running Acadex. This does not make files available to other computers automatically.

ONE-TIME SQLITE SETUP
---------------------
Your current MinGW.org GCC 6.3 compiler does not necessarily include SQLite development
files. Download the official SQLite amalgamation ZIP from:
https://www.sqlite.org/download.html

Choose "sqlite-amalgamation-3540100.zip" (SQLite 3.54.1) or another amalgamation release.
Extract it, then copy BOTH files below into this folder beside main.cpp:
  sqlite3.c
  sqlite3.h

The amalgamation contains the SQLite engine source and its matching C API header.
Do not rename these files.

BUILD AND RUN
-------------
cd /d/PBL/Acadex_SQL_New
cp /ucrt64/bin/libgcc_s_seh-1.dll .
cp /ucrt64/bin/libstdc++-6.dll .
cp /ucrt64/bin/libwinpthread-1.dll .
./acadex.exe
admin :- ID = admin


VIEW THE DATABASE
-----------------
Install DB Browser for SQLite from https://sqlitebrowser.org/.
Open acadex.db, choose Browse Data, then inspect these tables:
  students, crs, notes, pyqs, notifications

The notes/pyqs tables contain the file paths. The actual PDF bytes are in study_material/.
The database file is created automatically when Acadex starts.

MIGRATION / BACKUP
------------------
If data/students.txt, crs.txt, notes.txt, pyqs.txt, or notifications.txt exist from the
previous version, Acadex attempts to import those records the first time the corresponding
SQLite table is empty. Keep the old data folder as a backup until you verify the import.

Back up BOTH acadex.db and study_material/ before replacing project files or deleting data.
Deleting a catalogue record currently removes its database entry, not the PDF file itself.

SECURITY LIMITATIONS
--------------------
This is an educational prototype, not a production-ready multi-user service. Its password
hashing is demonstration-only, and the demo admin login remains hardcoded as admin/admin123.
Do not use real users' sensitive information or expose the database publicly without first
improving authentication, password hashing, authorization, and backups.
