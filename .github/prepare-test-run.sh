#! /usr/bin/env bash

# This script is used to prepare the test run environment on Github Actions.

DBMS="$1" # One of: "SQLite3", "MS SQL Server 2019", "MS SQL Server 2022" "PostgreSQL", "Oracle", "MySQL"

# Password to be set for the test suite with sufficient permissions (CREATE DATABASE, DROP DATABASE, ...)
DB_PASSWORD="BlahThat."
DB_NAME="LightweightTest"

# Creates .test-env.yml file with the connection string
# Usage: create_test_env_file <env_name> <connection_string>
create_test_env_file() {
    local env_name="$1"
    local connection_string="$2"
    local yaml_file=".test-env.yml"

    echo "Creating ${yaml_file} with environment '${env_name}'..."
    cat > "${yaml_file}" <<EOF
ODBC_CONNECTION_STRING:
  ${env_name}: "${connection_string}"
EOF

    if [ -n "$GITHUB_OUTPUT" ]; then
        echo "TEST_ENV_NAME=${env_name}" >> "${GITHUB_OUTPUT}"
    fi
}

setup_sqlite3() {
    echo "Setting up SQLite3..."
    sudo apt install -y \
                 libsqlite3-dev \
                 libsqliteodbc \
                 sqlite3 \
                 unixodbc-dev

    local connection_string="DRIVER=SQLite3;DATABASE=test.db"
    create_test_env_file "sqlite" "${connection_string}"

    if [ -n "$GITHUB_OUTPUT" ]; then
        echo "Exporting ODBC_CONNECTION_INFO..."
        echo "ODBC_CONNECTION_STRING=${connection_string}" >> "${GITHUB_OUTPUT}"
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

    local connection_string="DRIVER={ODBC Driver 18 for SQL Server};SERVER=localhost;PORT=1433;UID=SA;PWD=${DB_PASSWORD};TrustServerCertificate=yes;DATABASE=${DB_NAME}"
    create_test_env_file "mssql${SS_VERSION}" "${connection_string}"

    if [ -n "$GITHUB_OUTPUT" ]; then
        echo "Exporting ODBC_CONNECTION_INFO..."
        echo "ODBC_CONNECTION_STRING=${connection_string}" >> "${GITHUB_OUTPUT}"
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

    local connection_string="Driver={PostgreSQL Unicode};Server=localhost;Port=5432;Uid=$DB_USER;Pwd=$DB_PASSWORD;Database=$DB_NAME"
    create_test_env_file "pgsql" "${connection_string}"

    if [ -n "$GITHUB_OUTPUT" ]; then
        echo "Exporting ODBC_CONNECTION_INFO..."
        echo "ODBC_CONNECTION_STRING=${connection_string}" >> "${GITHUB_OUTPUT}"
    fi
}

setup_oracle() {
    echo "Setting up Oracle..." # TODO
    local ORACLE_VERSION="$1" # e.g. "23.5", "23.2", ...
    local client_tool_ver="21.3.0.0.0"
    local oracle_odbc_ver_major=21
    local DB_USER=$USER
    local target_dir="$HOME/oracle"

    # Override DB_NAME to FREEPDB1, as this is already created in the Oracle Docker image
    # and it helps avoiding unnecessary database creation time.
    DB_NAME="FREEPDB1"

    set -ex

    # References
    # - https://github.com/gvenzl/oci-oracle-free

    # install Oracle SQL server on ubuntu
    docker pull gvenzl/oracle-free:$ORACLE_VERSION
    docker run -d -p 1521:1521 \
               -e ORACLE_PASSWORD="$DB_PASSWORD" \
               -e APP_USER="$DB_USER" \
               -e APP_USER_PASSWORD="$DB_PASSWORD" \
               gvenzl/oracle-free:$ORACLE_VERSION

    # {{{ instant client
    wget https://download.oracle.com/otn_software/linux/instantclient/213000/instantclient-basiclite-linux.x64-${client_tool_ver}.zip
    wget https://download.oracle.com/otn_software/linux/instantclient/213000/instantclient-sqlplus-linux.x64-${client_tool_ver}.zip
    wget https://download.oracle.com/otn_software/linux/instantclient/213000/instantclient-odbc-linux.x64-${client_tool_ver}.zip
    unzip instantclient-basiclite-linux.x64-${client_tool_ver}.zip -d "${target_dir}"
    unzip instantclient-sqlplus-linux.x64-${client_tool_ver}.zip -d "${target_dir}"
    unzip instantclient-odbc-linux.x64-${client_tool_ver}.zip -d "${target_dir}"

    echo "${target_dir}/instantclient_21_3" | sudo tee /etc/ld.so.conf.d/oracle-instantclient.conf
    sudo ldconfig

    # The script `odbc_update_ini.sh` expects the current directory to be the instantclient directory
    cd "${target_dir}/instantclient_21_3"
    mkdir etc
    cp /etc/odbcinst.ini etc/
    cp /etc/odbc.ini etc/odbc.ini
    ./odbc_update_ini.sh .
    sudo cp -v etc/odbcinst.ini /etc/

    sudo apt install -y libaio-dev
    sudo ln -s /usr/lib/x86_64-linux-gnu/libaio.so.1t64 /usr/lib/x86_64-linux-gnu/libaio.so.1
    sudo ldconfig

    sudo odbcinst -q -d
    sudo odbcinst -q -d -n "Oracle ${oracle_odbc_ver_major} ODBC driver"
    # }}}

    # test connection with:
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${target_dir}/instantclient_21_3
    ldd ${target_dir}/instantclient_21_3/sqlplus
    DB_USER=system
    echo "SELECT table_name FROM user_tables WHERE ROWNUM <= 5;" \
        | ${target_dir}/instantclient_21_3/sqlplus -S "$DB_USER/$DB_PASSWORD@localhost:1521/$DB_NAME"

    local connection_string="DRIVER=Oracle ${oracle_odbc_ver_major} ODBC driver;SERVER=localhost;PORT=1521;UID=$DB_USER;PWD=$DB_PASSWORD;DBQ=$DB_NAME;DBA=W"
    create_test_env_file "oracle" "${connection_string}"

    if [ -n "$GITHUB_OUTPUT" ]; then
        echo "Exporting ODBC_CONNECTION_INFO..."
        echo "ODBC_CONNECTION_STRING=${connection_string}" >> "${GITHUB_OUTPUT}"
    fi
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
        setup_oracle 23.5
        ;;
    "MySQL")
        setup_mysql
        ;;
    *)
        echo "Unknown DBMS: $DBMS"
        exit 1
        ;;
esac

# Second argument is the file to load into the database
if [ -n "$2" ]; then
    echo "Loading data into the database..."
    file_name="$2"
    sqlcmd -S localhost -U SA -P "${DB_PASSWORD}" -d "${DB_NAME}" -i "${file_name}"
else
    echo "No file to load into the database."
fi
