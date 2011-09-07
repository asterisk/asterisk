drop table extensions_conf;

CREATE TABLE extensions_conf (
id serial NOT NULL,
context character varying(20) DEFAULT '' NOT NULL,
exten character varying(20) DEFAULT '' NOT NULL,
priority smallint DEFAULT 0 NOT NULL,
app character varying(20) DEFAULT '' NOT NULL,
appdata character varying(128)
);

drop table cdr;
CREATE TABLE cdr (
calldate timestamp with time zone DEFAULT now() NOT NULL,
clid character varying(80) DEFAULT '' NOT NULL,
src character varying(80) DEFAULT '' NOT NULL,
dst character varying(80) DEFAULT '' NOT NULL,
dcontext character varying(80) DEFAULT '' NOT NULL,
channel character varying(80) DEFAULT '' NOT NULL,
dstchannel character varying(80) DEFAULT '' NOT NULL,
lastapp character varying(80) DEFAULT '' NOT NULL,
lastdata character varying(80) DEFAULT '' NOT NULL,
duration bigint DEFAULT 0::bigint NOT NULL,
billsec bigint DEFAULT 0::bigint NOT NULL,
disposition character varying(45) DEFAULT '' NOT NULL,
amaflags bigint DEFAULT 0::bigint NOT NULL,
accountcode character varying(20) DEFAULT '' NOT NULL,
uniqueid character varying(32) DEFAULT '' NOT NULL,
userfield character varying(255) DEFAULT '' NOT NULL
);

drop table sip_conf;
CREATE TABLE sip_conf (
id serial NOT NULL,
name character varying(80) DEFAULT '' NOT NULL,
accountcode character varying(20),
amaflags character varying(7),
callgroup character varying(10),
callerid character varying(80),
canreinvite character varying(3) DEFAULT 'yes',
context character varying(80),
defaultip character varying(15),
dtmfmode character varying(7),
fromuser character varying(80),
fromdomain character varying(80),
host character varying(31) DEFAULT '' NOT NULL,
insecure character varying(4),
"language" character varying(2),
mailbox character varying(50),
md5secret character varying(80),
nat character varying(5) DEFAULT 'no' NOT NULL,
permit character varying(95),
deny character varying(95),
mask character varying(95),
pickupgroup character varying(10),
port character varying(5) DEFAULT '' NOT NULL,
qualify character varying(3),
restrictcid character varying(1),
rtptimeout character varying(3),
rtpholdtimeout character varying(3),
secret character varying(80),
"type" character varying DEFAULT 'friend' NOT NULL,
username character varying(80) DEFAULT '' NOT NULL,
allow character varying(200) DEFAULT '!all,g729,ilbc,gsm,ulaw,alaw',
musiconhold character varying(100),
regseconds bigint DEFAULT 0::bigint NOT NULL,
ipaddr character varying(40) DEFAULT '' NOT NULL,
regexten character varying(80) DEFAULT '' NOT NULL,
cancallforward character varying(3) DEFAULT 'yes',
lastms integer DEFAULT -1 NOT NULL
);

drop table voicemail_users;
CREATE TABLE voicemail_users (
id serial NOT NULL,
customer_id bigint DEFAULT (0)::bigint NOT NULL,
context character varying(50) DEFAULT '' NOT NULL,
mailbox bigint DEFAULT (0)::bigint NOT NULL,
"password" character varying(4) DEFAULT '0' NOT NULL,
fullname character varying(50) DEFAULT '' NOT NULL,
email character varying(50) DEFAULT '' NOT NULL,
pager character varying(50) DEFAULT '' NOT NULL,
stamp timestamp(6) without time zone NOT NULL
);

drop table queue_table;
CREATE TABLE queue_table (
name varchar(128),
musiconhold varchar(128),
announce varchar(128),
context varchar(128),
timeout int8,
monitor_join bool,
monitor_format varchar(128),
queue_youarenext varchar(128),
queue_thereare varchar(128),
queue_callswaiting varchar(128),
queue_holdtime varchar(128),
queue_minutes varchar(128),
queue_seconds varchar(128),
queue_lessthan varchar(128),
queue_thankyou varchar(128),
queue_reporthold varchar(128),
announce_frequency int8,
announce_round_seconds int8,
announce_holdtime varchar(128),
retry int8,
wrapuptime int8,
maxlen int8,
servicelevel int8,
strategy varchar(128),
joinempty varchar(128),
leavewhenempty varchar(128),
eventmemberstatus bool,
eventwhencalled bool,
reportholdtime bool,
memberdelay int8,
weight int8,
timeoutrestart bool,
setinterfacevar bool,
PRIMARY KEY (name)
) WITHOUT OIDS;
ALTER TABLE queue_table OWNER TO asterisk;

drop table queue_member_table;
CREATE TABLE queue_member_table
(
queue_name varchar(128),
interface varchar(128),
penalty int8,
PRIMARY KEY (queue_name, interface)
) WITHOUT OIDS;

GRANT ALL ON TABLE cdr TO asterisk;
GRANT ALL ON TABLE extensions_conf TO asterisk;
GRANT ALL ON TABLE sip_conf TO asterisk;
GRANT ALL ON TABLE voicemail_users TO asterisk;
GRANT ALL ON TABLE queue_member_table TO asterisk;
GRANT ALL ON TABLE queue_table TO asterisk;



