-- 
-- Table structure for Realtime meetme
-- 

CREATE TABLE meetme (
	confno char(80) DEFAULT '0' NOT NULL,
	-- Web booking id for the conference
	bookId char(50) NULL,
	-- Must set schedule=yes in meetme.conf to use starttime and endtime
	starttime datetime NULL,
	endtime datetime NULL,
	-- PIN to enter the conference, if any
	pin char(30) NULL,
	-- Options to associate with normal users of the conference
	opts char(100) NULL,
	-- PIN to enter the conference as an administrator, if any
	adminpin char(30) NULL,
	-- Options to associate with administrator users of the conference
	adminopts char(100) NULL,
	-- Current count of conference participants
	members integer DEFAULT 0 NOT NULL,
	-- Maximum conference participants allowed concurrently
	maxusers integer DEFAULT 0 NOT NULL,
	-- Recording of the conference, if any
	recordingfilename char(255) NULL,
	-- File format of the conference recording, if any
	recordingformat char(10) NULL,
	PRIMARY KEY (confno, starttime)
);

