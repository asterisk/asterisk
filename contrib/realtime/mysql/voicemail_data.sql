DROP TABLE IF EXISTS voicemail_data;
CREATE TABLE voicemail_data (
	-- Path to the recording
	filename CHAR(255) NOT NULL PRIMARY KEY,
	-- Mailbox number (without context)
	origmailbox CHAR(80),
	-- Dialplan context
	context CHAR(80),
	-- Dialplan context, if voicemail was invoked from a macro
	macrocontext CHAR(80),
	-- Dialplan extension
	exten CHAR(80),
	-- Dialplan priority
	priority INT(5),
	-- Name of the channel, when message was left
	callerchan CHAR(80),
	-- CallerID on the channel, when message was left
	callerid CHAR(80),
	-- Contrary to the name, origdate is a full datetime, in localized format
	origdate CHAR(30),
	-- Same date as origdate, but in Unixtime
	origtime INT(11),
	-- Value of the channel variable VM_CATEGORY, if set
	category CHAR(30),
	-- Length of the message, in seconds
	duration INT(11)
);


