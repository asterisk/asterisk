#
# Table structure for table `iaxfriends`
#

CREATE TABLE `iaxfriends` (
  `name` varchar(40) NOT NULL default '',
  `secret` varchar(40) NOT NULL default '',
  `context` varchar(40) NOT NULL default '',
  `ipaddr` varchar(20) NOT NULL default '',
  `port` int(6) NOT NULL default '0',
  `regseconds` int(11) NOT NULL default '0',
  `accountcode` varchar(20) NOT NULL default '', 
  PRIMARY KEY  (`name`)
) TYPE=MyISAM;

