
To setup docker image for this example:

Create empty mssql database 

``` sh
docker run -e "ACCEPT_EULA=Y" -e "MSSQL_SA_PASSWORD=BlahThat." -p 1433:1433 --name sql22 --hostname sql22 -d mcr.microsoft.com/mssql/server:2022-latest
```

Create database 

``` sh
sqlcmd -S localhost -U sa -P "BlahThat." -Q "CREATE DATABASE LightweightTest" -C
```

Load sql file to populate schema and data

``` sh
sqlcmd -S localhost -U sa -P "BlahThat." -C -d LightweightTest -i ./src/examples/test_chinook_modules/Chinook_Sqlite.sql
```

Create header file for every table in the directory `./src/examples/test_chinook_modules/entities` 

``` sh
./build/src/tools/ddl2cpp --connection-string "DRIVER={ODBC Driver 18 for SQL Server};SERVER=localhost;UID=sa;PWD=BlahThat.;TrustServerCertificate=yes;DATABASE=LightweightExample" --make-aliases --database LightweightExample --schema dbo --output ./src/examples/test_chinook_modules/entities
```

Make sure to configure the project with `-DLIGHTWEIGHT_BUILD_MODULES=ON`:

```sh
cmake -B build -DLIGHTWEIGHT_BUILD_MODULES=ON
cmake --build build
```
