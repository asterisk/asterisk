#
# Table structure for table `sipfriends`
#

CREATE TABLE `sipfriends` (
  `name` varchar(40) NOT NULL default '',
  `secret` varchar(40) NOT NULL default '',
  `context` varchar(40) NOT NULL default '',
  `username` varchar(40) default '',
  `ipaddr` varchar(20) NOT NULL default '',
  `port` int(6) NOT NULL default '0',
  `regseconds` int(11) NOT NULL default '0',
  PRIMARY KEY  (`name`)
) TYPE=MyISAM;
