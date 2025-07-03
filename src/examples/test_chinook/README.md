
To setup docker image for this example:



Create empty mssql database 

``` sh
docker run -e "ACCEPT_EULA=Y" -e "MSSQL_SA_PASSWORD=Qwerty1." -p 1433:1433 --name sql22 --hostname sql22 -d mcr.microsoft.com/mssql/server:2022-latest
```


Create database 

``` sh
sqlcmd -S localhost -U sa -P "Qwerty1." -Q "CREATE DATABASE LightweightTest" -C
```



Load sql file to populate schema and data

``` sh
sqlcmd -S localhost -U sa -P "Qwerty1." -C -d LightweightTest -i ./src/examples/test_chinook/Chinook_Sqlite.sql
```


Create header file for every table in the directory `./src/examples/test_chinook/entities` 

``` sh
./build/src/tools/ddl2cpp --connection-string "DRIVER={ODBC Driver 18 for SQL Server};SERVER=localhost;UID=sa;PWD=Qwerty1.;TrustServerCertificate=yes;DATABASE=LightweightExample" --make-aliases --database LightweightExample --schema dbo --output ./src/examples/test_chinook/entities
```
