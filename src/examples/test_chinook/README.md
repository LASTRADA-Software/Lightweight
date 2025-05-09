
To setup docker image for this example:



Create empty mssql database 

``` sh
docker run -e "ACCEPT_EULA=Y" -e "MSSQL_SA_PASSWORD=QWERT1.qwerty" -p 1433:1433 --name sql22 --hostname sql22 -d mcr.microsoft.com/mssql/server:2022-latest
```


Create database 

``` sh
sqlcmd -S localhost -U sa -P "QWERT1.qwerty" -Q "CREATE DATABASE LightweightExample" -C
```



Load sql file to populate schema and data

``` sh
sqlcmd -S localhost -U sa -P "QWERT1.qwerty" -C -d LightweightExample -i ./src/examples/test_chinook/Chinook_Sqlite.sql
```


Create header file for every table in the directory `./src/examples/test_chinook/entities` 

``` sh
./build/src/tools/ddl2cpp --connection-string "DRIVER={ODBC Driver 18 for SQL Server};SERVER=localhost;UID=sa;PWD=QWERT1.qwerty;TrustServerCertificate=yes;DATABASE=LightweightExample" --make-aliases --database LightweightExample --schema dbo --foreign-key-collision-prefix fk_ --output ./src/examples/test_chinook/entities
```
