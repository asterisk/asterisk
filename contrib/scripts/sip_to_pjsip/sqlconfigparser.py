from astconfigparser import MultiOrderedConfigParser

try:
    import pymysql as MySQLdb
    MySQLdb.install_as_MySQLdb()
except ImportError:
    # MySQLdb is compatible with Python 2 only.  Try it as a
    # fallback if pymysql is unavailable.
    import MySQLdb

import traceback

class SqlConfigParser(MultiOrderedConfigParser):

    _tablename = "sippeers"

    def __init__(self,tablename="sippeers"):
        self._tablename=tablename
        MultiOrderedConfigParser.__init__(self)

    def connect(self, user, password, host, port, database):
        self.cnx = MySQLdb.connect(user=user,passwd=password,host=host,port=int(port),db=database)

    def read(self, filename, sect=None):
        MultiOrderedConfigParser.read(self, filename, sect)
        # cursor = self.cnx.cursor(dictionary=True)
        cursor = self.cnx.cursor(cursorclass=MySQLdb.cursors.DictCursor)
        cursor.execute("SELECT * from `" + MySQLdb.escape_string(self._tablename) + "`")
        rows = cursor.fetchall()

        for row in rows:
            sect = self.add_section(row['name'])
            for key in row:
                if (row[key] != None):
                    for elem in str(row[key]).split(";"):
                        sect[key] = elem
                    #sect[key] = str(row[key]).split(";")

    def write_dicts(self, config_file, mdicts):
        """Write the contents of the mdicts to the specified config file"""
        for section, sect_list in mdicts.iteritems():
            # every section contains a list of dictionaries
            for sect in sect_list:
                sql = "INSERT INTO "
                if (sect.get('type')[0] == "endpoint"):
                    sql += "ps_endpoints "
                elif (sect.get('type')[0] == "aor" and section != "sbc"):
                    sql += "ps_aors "
                elif (sect.get('type')[0] == "identify"):
                    sql += "ps_endpoint_id_ips"
                else:
                    continue

                sql += " SET `id` = " + "\"" + MySQLdb.escape_string(section) + "\""
                for key, val_list in sect.iteritems():
                    if key == "type":
                        continue
                    # every value is also a list

                    key_val = " `" + key + "`"
                    key_val += " = " + "\"" + MySQLdb.escape_string(";".join(val_list)) + "\""
                    sql += ","
                    sql += key_val

                config_file.write("%s;\n" % (sql))

    def write(self, config_file):
        """Write configuration information out to a file"""
        try:
            self.write_dicts(config_file, self._sections)
        except:
                print("Could not open file " + config_file + " for writing")
                traceback.print_exc()
