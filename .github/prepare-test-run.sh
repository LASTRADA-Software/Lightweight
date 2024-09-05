#! /usr/bin/env bash

# This script is used to prepare the test run environment on Github Actions.

DBMS="$1" # One of: "SQLite3", "MS SQL Server 2019", "MS SQL Server 2022" "PostgreSQL", "Oracle", "MySQL"

# Password to be set for the test suite with sufficient permissions (CREATE DATABASE, DROP DATABASE, ...)
DB_PASSWORD="BlahThat."
DB_NAME="JPSqlTest"

setup_sqlite3() {
    echo "Setting up SQLite3..."
    sudo apt install -y \
                 libsqlite3-dev \
                 libsqliteodbc \
                 sqlite3 \
                 unixodbc-dev

    if [ -n "$GITHUB_OUTPUT" ]; then
        echo "Exporting ODBC_CONNECTION_INFO..."
        # expose the ODBC connection string to connect to the database
        echo "ODBC_CONNECTION_STRING=DRIVER=SQLite3;DATABASE=file::memory:" >> "${GITHUB_OUTPUT}"
    fi
}

setup_sqlserver() {
    # References:
    # https://learn.microsoft.com/en-us/sql/linux/sample-unattended-install-ubuntu?view=sql-server-ver16
    # https://learn.microsoft.com/en-us/sql/tools/sqlcmd/sqlcmd-utility
    # https://learn.microsoft.com/en-us/sql/linux/quickstart-install-connect-docker

    set -x
    local MSSQL_PID='evaluation'
    local SS_VERSION="$1"
    local UBUNTU_RELEASE="20.04" # we fixiate the version, because the latest isn't always available by MS -- "$(lsb_release -r | awk '{print $2}')

    echo "Installing sqlcmd..."
    curl https://packages.microsoft.com/keys/microsoft.asc | sudo tee /etc/apt/trusted.gpg.d/microsoft.asc
    sudo add-apt-repository "$(wget -qO- https://packages.microsoft.com/config/ubuntu/${UBUNTU_RELEASE}/prod.list)"
    sudo apt update
    sudo apt install sqlcmd

    echo "Installing ODBC..."
    sudo ACCEPT_EULA=y DEBIAN_FRONTEND=noninteractive apt install -y unixodbc-dev unixodbc odbcinst mssql-tools18
    dpkg -L mssql-tools18

    echo "ODBC drivers installed:"
    sudo odbcinst -q -d

    echo "Querying ODBC driver for MS SQL Server..."
    sudo odbcinst -q -d -n "ODBC Driver 18 for SQL Server"

    echo "Pulling SQL Server ${SS_VERSION} image..."
    docker pull mcr.microsoft.com/mssql/server:${SS_VERSION}-latest

    echo "Starting SQL Server ${SS_VERSION}..."
    docker run \
            -e "ACCEPT_EULA=Y" \
            -e "MSSQL_SA_PASSWORD=${DB_PASSWORD}" \
            -p 1433:1433 --name sql${SS_VERSION} --hostname sql${SS_VERSION} \
            -d \
            "mcr.microsoft.com/mssql/server:${SS_VERSION}-latest"

    docker ps -a
    set +x

    echo "Wait for the SQL Server to start..."
    counter=1
    errstatus=1
    while [ $counter -le 15 ] && [ $errstatus = 1 ]
    do
        echo "$counter..."
        sleep 1s
        sqlcmd \
            -S localhost \
            -U SA \
            -P "$DB_PASSWORD" \
            -Q "SELECT @@VERSION" 2>/dev/null
        errstatus=$?
        ((counter++))
    done

    # create a test database
    sqlcmd -S localhost -U SA -P "${DB_PASSWORD}" -Q "CREATE DATABASE ${DB_NAME}"

    if [ -n "$GITHUB_OUTPUT" ]; then
        echo "Exporting ODBC_CONNECTION_INFO..."
        # expose the ODBC connection string to connect to the database server
        echo "ODBC_CONNECTION_STRING=DRIVER={ODBC Driver 18 for SQL Server};SERVER=localhost;PORT=1433;UID=SA;PWD=${DB_PASSWORD};TrustServerCertificate=yes;DATABASE=${DB_NAME}" >> "${GITHUB_OUTPUT}"
    fi
}

setup_postgres() {
    echo "Setting up PostgreSQL..."
    # For Fedora: sudo dnf -y install postgresql-server postgresql-odbc
    sudo apt install -y \
                postgresql \
                postgresql-contrib \
                libpq-dev \
                odbc-postgresql

    sudo postgresql-setup --initdb --unit postgresql

    # check Postgres, version, and ODBC installation
    sudo systemctl start postgresql
    psql -V
    odbcinst -q -d
    odbcinst -q -d -n "PostgreSQL ANSI"
    odbcinst -q -d -n "PostgreSQL Unicode"

    echo "Wait for the PostgreSQL server to start..."
    counter=1
    errstatus=1
    while [ $counter -le 15 ] && [ $errstatus = 1 ]
    do
        echo "$counter..."
        pg_isready -U postgres
        errstatus=$?
        ((counter++))
    done

    echo "ALTER USER postgres WITH PASSWORD '$DB_PASSWORD';" > setpw.sql
    sudo -u postgres psql -f setpw.sql
    rm setpw.sql

    echo "Create database user..."
    local DB_USER="$USER"
    sudo -u postgres psql -c "CREATE USER $DB_USER WITH SUPERUSER PASSWORD '$DB_PASSWORD'"

    echo "Create database..."
    sudo -u postgres createdb $DB_NAME

    if [ -n "$GITHUB_OUTPUT" ]; then
        echo "Exporting ODBC_CONNECTION_INFO..."
        echo "ODBC_CONNECTION_STRING=Driver={PostgreSQL ANSI};Server=localhost;Port=5432;Uid=$DB_USER;Pwd=$DB_PASSWORD;Database=$DB_NAME" >> "${GITHUB_OUTPUT}"
    fi
}

setup_oracle() {
    echo "Setting up Oracle..." # TODO

    # References
    # - https://github.com/gvenzl/oci-oracle-free

    # {{{ install Oracle SQL server on ubuntu

    local DB_PASSWORD="BlahThat."
    local ORACLE_VERSION="$1" # e.g. "23.5", "23.2", ...
    docker pull gvenzl/oracle-free:$ORACLE_VERSION
    docker run -d -p 1521:1521 -e ORACLE_PASSWORD="$DB_PASSWORD" gvenzl/oracle-free:$ORACLE_VERSION

    # }}}

    # {{{ instant client
    wget https://download.oracle.com/otn_software/linux/instantclient/213000/instantclient-basiclite-linux.x64-21.3.0.0.0.zip
    wget https://download.oracle.com/otn_software/linux/instantclient/213000/instantclient-sqlplus-linux.x64-21.3.0.0.0.zip
    wget https://download.oracle.com/otn_software/linux/instantclient/213000/instantclient-odbc-linux.x64-21.3.0.0.0.zip
    unzip instantclient-basiclite-linux.x64-21.3.0.0.0.zip
    unzip instantclient-sqlplus-linux.x64-21.3.0.0.0.zip
    unzip instantclient-odbc-linux.x64-21.3.0.0.0.zip
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PWD/instantclient_21_3

    cd instantclient_21_3
    mkdir etc
    cp /etc/odbcinst.ini etc/.
    cp ~/.odbc.ini etc/odbc.ini
    ./odbc_update_ini.sh .
    sudo cp etc/odbcinst.ini /etc/

    odbcinst -q -d
    odbcinst -q -d -n "Oracle 21 ODBC driver"

    # test connection (interactively) with:
    ./sqlplus scott/tiger@'(DESCRIPTION = (ADDRESS = (PROTOCOL = TCP)(HOST = db)(PORT = 1521)) (CONNECT_DATA = (SERVER = DEDICATED) (SERVICE_NAME = orcl)))'

    # show version to console

    # }}}
}

setup_mysql() {
    # install mysql server and its odbc driver
    sudo apt install -y mysql-server # TODO: odbc driver
}

case "$DBMS" in
    "SQLite3")
        setup_sqlite3
        ;;
    "MS SQL Server 2019")
        setup_sqlserver 2019
        ;;
    "MS SQL Server 2022")
        setup_sqlserver 2022
        ;;
    "PostgreSQL")
        setup_postgres
        ;;
    "Oracle")
        setup_oracle
        ;;
    "MySQL")
        setup_mysql
        ;;
    *)
        echo "Unknown DBMS: $DBMS"
        exit 1
        ;;
esac
