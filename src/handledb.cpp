#include "../include/handledb.hpp"
using namespace Tools;

void DataBase::initDb(const char* dbPath){

    rc = sqlite3_open(dbPath, &db);
    if(rc != SQLITE_OK){
        cerr << "Error: " << sqlite3_errmsg(db) << endl;
        sqlite3_close(db);
        db = nullptr;
    }

    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY AUTOINCREMENT, context TEXT NOT NULL, response TEXT NOT NULL, model TEXT NOT NULL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)", nullptr, nullptr, nullptr);
    if(rc != SQLITE_OK){
        cerr << "Error: " << sqlite3_errmsg(db) << endl;
        sqlite3_close(db);
        db = nullptr;
    }
}

DataBase::~DataBase(){
    if(db){
        sqlite3_close(db);
    }
}