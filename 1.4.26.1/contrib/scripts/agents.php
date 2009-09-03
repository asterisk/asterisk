<?php 

ob_implicit_flush(false);

$username = "drmac";
$secret   = "secret";

$socket = fsockopen("127.0.0.1","5038", $errornum, $errorstr);

$agents = array();
$curr_agent = "";
$better_status = array(	'AGENT_UNKNOWN' 	=> 'Unknown',
			'AGENT_IDLE' 		=> 'Idle',
			'AGENT_ONCALL' 		=> 'On Call',
			'AGENT_LOGGEDOFF' 	=> 'Not Logged In' );

if(!$socket) {
	print "Couldn't open socket. Error #" . $errornum . ": " . $errorstr;
} else {
	fputs($socket, "Action: Login\r\n");
	fputs($socket, "UserName: $username\r\n");
	fputs($socket, "Secret: $secret\r\n\r\n");
	fputs($socket, "Action: Agents\r\n\r\n");
	fputs($socket, "Action: Logoff\r\n\r\n");

	while(!feof($socket)) {
		$info = fscanf($socket, "%s\t%s\r\n");
		switch($info[0]) {
			case "Agent:":
				$curr_agent = $info[1];
				$agents[$curr_agent] = array();
				break;
			case "Name:":
				$agents[$curr_agent]['Name'] = $info[1];
				break;
			case "Status:":
				$agents[$curr_agent]['Status'] = $better_status[$info[1]];
				break;
			case "LoggedInChan:":
				$agents[$curr_agent]['LoggedInChan'] = $info[1];
				break;
			case "LoggedInTime:":
				if($info[1] != "0") {
					$agents[$curr_agent]['LoggedInTime'] = date("D, M d Y g:ia", $info[1]);
				} else {
					$agents[$curr_agent]['LoggedInTime'] = "n/a";
				}
				break;
			case "TalkingTo:":
				$agents[$curr_agent]['TalkingTo'] = $info[1];
				break;
			default:
				break;
		}
	}
	fclose($socket);

	print "<html><head><title>Agents Status</title></head>\n<body>\n";
	print "<table width=\"800px\" border=\"1\">\n";
	print "  <tr><th>Agent #</th><th>Agent Name</th><th>Agent Location</th><th>Agent Status</th><th>Agent Talking To</th><th>Agent Login Time</th></tr>\n";

	foreach( $agents as $agent=>$curr ) {
		print "  <tr>\n    <td>" . $agent . "</td>\n";
		print "    <td>" . $curr['Name'] . "</td>\n";
		print "    <td>" . $curr['LoggedInChan'] . "</td>\n";
		print "    <td>" . $curr['Status'] . "</td>\n";
		print "    <td>" . $curr['TalkingTo'] . "</td>\n";
		print "    <td>" . $curr['LoggedInTime'] . "</td>\n  </tr>\n";
	}

	print "</table>\n</body>\n</html>\n";
}
?>
