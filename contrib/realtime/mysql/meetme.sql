-- 
-- Table structure for Realtime meetme
-- 

CREATE TABLE meetme (
	confno char(80) DEFAULT '0' NOT NULL,
	pin char(20) NULL,
	adminpin char(20) NULL,
	members integer DEFAULT 0 NOT NULL,
	PRIMARY KEY (confno)
);

