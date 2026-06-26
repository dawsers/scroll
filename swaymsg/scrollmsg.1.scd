scrollmsg(1)

# NAME

scrollmsg - Send messages to a running instance of scroll over the IPC socket.

# SYNOPSIS

_scrollmsg_ [options...] [message]

# OPTIONS

*-h, --help*
	Show help message and quit.

*-m, --monitor*
	Monitor for responses until killed instead of exiting after the first
	response. This can only be used with the IPC message type _subscribe_. If
	there is a malformed response or an invalid event type was requested,
	scrollmsg will stop monitoring and exit.

*-p, --pretty*
	Use pretty output even when not using a tty.
	Not available for all message types.

*-q, --quiet*
	Sends the IPC message but does not print the response from scroll.

*-r, --raw*
	Use raw JSON output even if using a tty.

*-s, --socket* <path>
	Use the specified socket path. Otherwise, scrollmsg will ask scroll where the
	socket is (which is the value of $SCROLLSOCK, then of $SWAYSOCK and then $I3SOCK).

*-t, --type* <type>
	Specify the type of IPC message. See below.

*-v, --version*
	Print the version (of scrollmsg) and quit.

# IPC MESSAGE TYPES

*<command>*
	The message is a scroll command (the same commands you can bind to keybindings
	in your scroll config file). It will be executed immediately.

	See *scroll*(5) for a list of commands.

	_scrollmsg_ can return pretty printed (standalone-default) or JSON-formatted
	(*--raw*) output. For detailed documentation on the returned JSON-data of
	each message type listed below,	refer to *scroll-ipc*(7). The JSON-format can
	contain more information than the pretty print.

	Tips:
	- Command expansion is performed twice: once by scrollmsg, and again by scroll.
	  If you have quoted multi-word strings in your command, enclose the entire
	  command in single-quotes. For example, use
	  _scrollmsg 'output "Foobar Display" enable'_ instead of
	  _scrollmsg output "Foobar Display" enable_. Furthermore, note that comma
	  separated options also count as multi-word strings, because commas can be
	  used to execute commands on the same line.
	- If you are providing a command that contains a leading hyphen (_-_), insert
	  two hyphens (_--_) before the command to signal to scrollmsg not to parse
	  anything beyond that point as an option. For example, use
	  _scrollmsg -- mark --add test_ instead of _scrollmsg mark --add test_.

*get\_workspaces*
	Gets a list of workspaces and their status.

*get\_inputs*
	Gets a list of current inputs.

*get\_outputs*
	Gets a list of current outputs.

*get\_tree*
	Gets a JSON-encoded layout tree of all open windows, containers, outputs,
	workspaces, and so on.

*get\_seats*
	Gets a list of all seats,
	its properties and all assigned devices.

*get\_marks*
	Get a JSON-encoded list of marks.

*get\_bar\_config*
	Get a JSON-encoded configuration for scrollbar.

*get\_version*
	Get version information for the running instance of scroll.

*get\_binding\_modes*
	Gets a JSON-encoded list of currently configured binding modes.

*get\_binding\_state*
	Gets JSON-encoded info about the current binding state.

*get\_config*
	Gets a copy of the current configuration. Doesn't expand includes.

*get\_scroller*
	Gets the scroll modifiers for the current workspace.

*get\_trails*
	Gets the trails information.

*send\_tick*
	Sends a tick event to all subscribed clients.

*mint\_activation\_token*
	Mint a new XDG activation token.

*subscribe*
	Subscribe to a list of event types. The argument for this type should be
	provided in the form of a valid JSON array. If any of the types are invalid
	or if a valid JSON array is not provided, this will result in a failure.
	For a list of valid event types and the data returned with them refer to
	*scroll-ipc*(7).

# RETURN CODES

*0*
	Success

*1*
	scrollmsg errors such as invalid syntax, unable to connect to the ipc socket
	or unable to parse scroll's reply

*2*
	Scroll returned an error when processing the command (ex. invalid command,
	command failed, and invalid subscription request)

# SEE ALSO

*scroll*(5) *scroll-bar*(5) *scroll-input*(5) *scroll-output*(5) *scroll-ipc*(7)
