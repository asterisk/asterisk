DROP TABLE IF EXISTS voicemail;
CREATE TABLE voicemail (
	-- All of these column names are very specific, including "uniqueid".  Do not change them if you wish voicemail to work.
	uniqueid INT(5) NOT NULL AUTO_INCREMENT PRIMARY KEY,
	-- Mailbox context.
	context CHAR(80) NOT NULL DEFAULT 'default',
	-- Mailbox number.  Should be numeric.
	mailbox CHAR(80) NOT NULL,
	-- Must be numeric.  Negative if you don't want it to be changed from VoicemailMain
	password CHAR(80) NOT NULL,
	-- Used in email and for Directory app
	fullname CHAR(80),
	-- Email address (will get sound file if attach=yes)
	email CHAR(80),
	-- Email address (won't get sound file)
	pager CHAR(80),
	-- Attach sound file to email - YES/no
	attach CHAR(3),
	-- Send email from this address
	serveremail CHAR(80),
	-- Prompts in alternative language
	language CHAR(20),
	-- Alternative timezone, as defined in voicemail.conf
	tz CHAR(30),
	-- Delete voicemail from server after sending email notification - yes/NO
	deletevoicemail CHAR(3),
	-- Read back CallerID information during playback - yes/NO
	saycid CHAR(3),
	-- Allow user to send voicemail from within VoicemailMain - YES/no
	sendvoicemail CHAR(3),
	-- Listen to voicemail and approve before sending - yes/NO
	review CHAR(3),
	-- Allow '0' to jump out during greeting - yes/NO
	operator CHAR(3),
	-- Hear date/time of message within VoicemailMain - YES/no
	envelope CHAR(3),
	-- Hear length of message within VoicemailMain - yes/NO
	sayduration CHAR(3),
	-- Minimum duration in minutes to say
	saydurationm INT(3),
	-- Force new user to record name when entering voicemail - yes/NO
	forcename CHAR(3),
	-- Force new user to record greetings when entering voicemail - yes/NO
	forcegreetings CHAR(3),
	-- Context in which to dial extension for callback
	callback CHAR(80),
	-- Context in which to dial extension (from advanced menu)
	dialout CHAR(80),
	-- Context in which to execute 0 or * escape during greeting
	exitcontext CHAR(80),
	-- Maximum messages in a folder (100 if not specified)
	maxmsg INT(5),
	stamp timestamp
);
