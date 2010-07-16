CREATE TABLE musiconhold (
	-- Name of the MOH class
	name char(80) not null primary key,
	-- One of 'custom', 'files', 'mp3nb', 'quietmp3nb', or 'quietmp3'
	mode char(80) null,
	-- If 'custom', directory is ignored.  Otherwise, specifies a directory with files to play or a stream URL
	directory char(255) null,
	-- If 'custom', application will be invoked to provide MOH.  Ignored otherwise.
	application char(255) null,
	-- Digit associated with this MOH class, when MOH is selectable by the caller.
	digit char(1) null,
	-- One of 'random' or 'alpha', to determine how files are played.  If NULL, files are played in directory order
	sort char(10) null,
	-- In custom mode, the format of the audio delivered.  Ignored otherwise.  Defaults to SLIN.
	format char(10) null,
	-- When this record was last modified
	stamp timestamp
);

