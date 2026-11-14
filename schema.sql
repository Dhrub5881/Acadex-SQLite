-- Acadex creates these tables automatically on first launch.
CREATE TABLE IF NOT EXISTS students (
  id TEXT PRIMARY KEY COLLATE NOCASE, hash TEXT NOT NULL, name TEXT, phone TEXT, email TEXT,
  course TEXT, specialization TEXT, admission_year INTEGER, year INTEGER, semester INTEGER, section TEXT
);
CREATE TABLE IF NOT EXISTS crs (
  id TEXT PRIMARY KEY COLLATE NOCASE, hash TEXT NOT NULL, profile_complete INTEGER NOT NULL DEFAULT 0,
  name TEXT, phone TEXT, email TEXT, course TEXT, specialization TEXT, section TEXT
);
CREATE TABLE IF NOT EXISTS notes (
  id TEXT PRIMARY KEY, title TEXT, subject TEXT, unit TEXT, topic TEXT, path TEXT, course TEXT,
  specialization TEXT, section TEXT, semester INTEGER, uploader TEXT, uploaded_at TEXT
);
CREATE TABLE IF NOT EXISTS pyqs (
  id TEXT PRIMARY KEY, title TEXT, subject TEXT, unit TEXT, topic TEXT, path TEXT, course TEXT,
  specialization TEXT, section TEXT, semester INTEGER, uploader TEXT, uploaded_at TEXT
);
CREATE TABLE IF NOT EXISTS notifications (
  id INTEGER PRIMARY KEY AUTOINCREMENT, created_at TEXT NOT NULL, message TEXT NOT NULL
);
