-- While this does not use the realtime backend, for brevity, we include this table here, as well.
DROP TABLE IF EXISTS voicemail_messages;
CREATE TABLE voicemail_messages (
	-- Logical directory
	dir CHAR(255),
	-- Message number within the logical directory
	msgnum INT(4),
	-- Dialplan context
	context CHAR(80),
	-- Dialplan context, if Voicemail was invoked from a macro
	macrocontext CHAR(80),
	-- CallerID, when the message was left
	callerid CHAR(80),
	-- Date when the message was left, in Unixtime
	origtime INT(11),
	-- Length of the message, in seconds
	duration INT(11),
	-- The recording itself
	recording BLOB,
	-- Text flags indicating urgency of the message
	flag CHAR(30),
	-- Value of channel variable VM_CATEGORY, if set
	category CHAR(30),
	-- Owner of the mailbox
	mailboxuser CHAR(30),
	-- Context of the owner of the mailbox
	mailboxcontext CHAR(30),
	PRIMARY KEY (dir, msgnum)
);
