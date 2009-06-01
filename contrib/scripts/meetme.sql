-- 
-- Table structure for Realtime meetme
-- 

CREATE TABLE meetme (
	confno char(80) DEFAULT '0' NOT NULL,
	-- Must set schedule=yes in meetme.conf to use starttime and endtime
	starttime datetime NULL,
	endtime datetime NULL,
	-- PIN to enter the conference, if any
	pin char(20) NULL,
	-- PIN to enter the conference as an administrator, if any
	adminpin char(20) NULL,
	-- Current count of conference participants
	members integer DEFAULT 0 NOT NULL,
	-- Maximum conference participants allowed concurrently
	maxusers integer DEFAULT 0 NOT NULL,
	PRIMARY KEY (confno, starttime)
);

