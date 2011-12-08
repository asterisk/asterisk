CREATE TABLE queue_log (
	-- Event date and time
	time datetime,
	-- "REALTIME", "NONE", or channel uniqueid
	callid char(50),
	-- Name of the queue affected
	queuename char(50),
	-- Interface name of the queue member
	agent char(50),
	-- One of ADDMEMBER, REMOVEMEMBER, RINGNOANSWER, EXITEMPTY, TRANSFER,
	-- AGENTDUMP, ABANDON, SYSCOMPAT, CONNECT, COMPLETECALLER, COMPLETEAGENT,
	-- PAUSEALL, UNPAUSEALL, PAUSE, UNPAUSE, PENALTY, ENTERQUEUE,
	-- EXITWITHTIMEOUT, EXITEMPTY, EXITWITHKEY, or another defined by the user.
	event char(20),
	-- data1 through data5 are possible arguments to the event, the definitions
	-- of which are dependent upon the type of event.
	data1 char(50),
	data2 char(50),
	data3 char(50),
	data4 char(50),
	data5 char(50),
	index bydate (time),
	index qname (queuename,time)
);
