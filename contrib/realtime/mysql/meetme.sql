-- 
-- Table structure for Realtime meetme
-- 

CREATE TABLE meetme (
	bookid int(11) auto_increment,
	confno char(80) DEFAULT '0' NOT NULL,
	starttime datetime default '1900-01-01 12:00:00',
	endtime datetime default '2038-01-01 12:00:00',
	pin char(20) NULL,
	adminpin char(20) NULL,
	opts char(20) NULL,
	adminopts char(20) NULL,
	recordingfilename char(80) NULL,
	recordingformat char(10) NULL,
	maxusers int(11) NULL,
	members integer DEFAULT 0 NOT NULL,
	index confno (confno,starttime,endtime),
	PRIMARY KEY (bookid)
);

