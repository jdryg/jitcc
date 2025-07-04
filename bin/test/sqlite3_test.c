#include "sqlite3/sqlite3.h"
#include <stdio.h>

int main(void)
{
    sqlite3* db;
    char* err_msg = 0;

    int rc = sqlite3_open_v2("cars.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "win32");
    if (rc != SQLITE_OK) {
        printf("Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    const char* sql = "DROP TABLE IF EXISTS Cars;"
        "CREATE TABLE Cars(Id INT, Name TEXT, Price INT);"
        "INSERT INTO Cars VALUES(1, 'Audi', 52642);"
        "INSERT INTO Cars VALUES(2, 'Mercedes', 57127);"
        "INSERT INTO Cars VALUES(3, 'Skoda', 9000);"
        "INSERT INTO Cars VALUES(4, 'Volvo', 29000);"
        "INSERT INTO Cars VALUES(5, 'Bentley', 350000);"
        "INSERT INTO Cars VALUES(6, 'Citroen', 21000);"
        "INSERT INTO Cars VALUES(7, 'Hummer', 41400);"
        "INSERT INTO Cars VALUES(8, 'Volkswagen', 21600);";
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);

    return 0;
}

