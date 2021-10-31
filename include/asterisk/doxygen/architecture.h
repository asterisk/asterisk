/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 * \author Russell Bryant <russell@digium.com>
 */

/*!
\page AsteriskArchitecture Asterisk Architecture Overview
\author Russell Bryant <russell@digium.com>

<hr>

\section ArchTOC Table of Contents

 -# \ref ArchIntro
 -# \ref ArchLayout
 -# \ref ArchInterfaces
    -# \ref ArchInterfaceCodec
    -# \ref ArchInterfaceFormat
    -# \ref ArchInterfaceAPIs
    -# \ref ArchInterfaceAMI
    -# \ref ArchInterfaceChannelDrivers
    -# \ref ArchInterfaceBridge
    -# \ref ArchInterfaceCDR
    -# \ref ArchInterfaceCEL
    -# \ref ArchInterfaceDialplanApps
    -# \ref ArchInterfaceDialplanFuncs
    -# \ref ArchInterfaceRTP
    -# \ref ArchInterfaceTiming
 -# \ref ArchThreadingModel
    -# \ref ArchChannelThreads
    -# \ref ArchMonitorThreads
    -# \ref ArchServiceThreads
    -# \ref ArchOtherThreads
 -# \ref ArchConcepts
    -# \ref ArchConceptBridging
 -# \ref ArchCodeFlows
    -# \ref ArchCodeFlowPlayback
    -# \ref ArchCodeFlowBridge
 -# \ref ArchDataStructures
    -# \ref ArchAstobj2
    -# \ref ArchLinkedLists
    -# \ref ArchDLinkedLists
    -# \ref ArchHeap
 -# \ref ArchDebugging
    -# \ref ArchThreadDebugging
    -# \ref ArchMemoryDebugging

<hr>

\section ArchIntro Introduction

This section of the documentation includes an overview of the Asterisk architecture
from a developer's point of view.  For detailed API discussion, see the documentation
associated with public API header files.  This documentation assumes some knowledge
of what Asterisk is and how to use it.

The intent behind this documentation is to start looking at Asterisk from a high
level and progressively dig deeper into the details.  It begins with talking about
the different types of components that make up Asterisk and eventually will go
through interactions between these components in different use cases.

Throughout this documentation, many links are also provided as references to more
detailed information on related APIs, as well as the related source code to what
is being discussed.

Feedback and contributions to this documentation are very welcome.  Please send your
comments to the asterisk-dev mailing list on http://lists.digium.com/.

Thank you, and enjoy Asterisk!


\section ArchLayout Modular Architecture

Asterisk is a highly modularized application.  There is a core application that
is built from the source in the <code>main/</code> directory.  However, it is
not very useful by itself.

There are many modules that are loaded at runtime.  Asterisk modules have names that
give an indication as to what functionality they provide, but the name is not special
in any technical sense.  When Asterisk loads a module, the module registers the
functionality that it provides with the Asterisk core.

 -# Asterisk starts
 -# Asterisk loads modules
 -# Modules say "Hey Asterisk!  I am a module.  I can provide functionality X, Y,
    and Z.  Let me know when you'd like to use my functionality!"


\section ArchInterfaces Abstract Interface types

There are many types of interfaces that modules can implement and register their
implementations of with the Asterisk core.  Any module is allowed to register as
many of these different interfaces as they would like.  Generally, related
functionality is grouped into a single module.

In this section, the types of interfaces are discussed.  Later, there will
be discussions about how different components interact in various scenarios.

\subsection ArchInterfaceCodec Codec Interpreter

An implementation of the codec interpreter interface provides the ability to
convert between two codecs.  Asterisk currently only has the ability to translate
between audio codecs.

These modules have no knowledge about phone calls or anything else about why
they are being asked to convert audio.  They just get audio samples as input
in their specified input format, and are expected to provide audio in the
specified output format.

It is possible to have multiple paths to get from codec A to codec B once many
codec implementations are registered.  After modules have been loaded, Asterisk
builds a translation table with measurements of the performance of each codec
translator so that it can always find the best path to get from A to B.

Codec modules typically live in the <code>codecs/</code> directory in the
source tree.

For a list of codec interpreter implementations, see \ref codecs.

For additional information on the codec interpreter API, see the interface
definition in <code>include/asterisk/translate.h</code>.

For core implementation details related to the codec interpreter API, see
<code>main/translate.c</code>.

\subsection ArchInterfaceFormat File Format Handler

An implementation of the file format handler interface provides Asterisk the
ability to read and optionally write files.  File format handlers may provide
access to audio, video, or image files.

The interface for a file format handler is rather primitive.  A module simply
tells the Asterisk core that it can handle files with a given %extension,
for example, ".wav".  It also says that after reading the file, it will
provide audio in the form of codec X.  If a file format handler provides the
ability to write out files, it also must specify what codec the audio should
be in before provided to the file format handler.

File format modules typically live in the <code>formats/</code> directory in the
source tree.

For a list of file format handler implementations, see \ref formats.

For additional information on the file format handler API, see the interface
definition in <code>include/asterisk/file.h</code>.

For core implementation details related to the file format API, see
<code>main/file.c</code>.

\subsection ArchInterfaceAPIs C API Providers

There are some C APIs in Asterisk that are optional.  Core APIs are built into
the main application and are always available.  Optional C APIs are provided
by a module and are only available for use when the module is loaded.  Some of
these API providers also contain their own interfaces that other modules can
implement and register.

Modules that provide a C API typically live in the <code>res/</code> directory
in the source tree.

Some examples of modules that provide C APIs (potentially among other things) are:
 - res_musiconhold.c
 - res_calendar.c
   - provides a calendar technology interface.
 - res_odbc.c
 - res_ael_share.c
 - res_crypto.c
 - res_curl.c
 - res_xmpp.c
 - res_monitor.c
 - res_smdi.c
 - res_speech.c
   - provides a speech recognition engine interface.

\subsection ArchInterfaceAMI Manager Interface (AMI) Actions

The Asterisk manager interface is a socket interface for monitoring and control
of Asterisk.  It is a core feature built in to the main application.  However,
modules can register %actions that may be requested by clients.

Modules that register manager %actions typically do so as auxiliary functionality
to complement whatever main functionality it provides.  For example, a module that
provides call conferencing services may have a manager action that will return the
list of participants in a conference.

\subsection ArchInterfaceCLI CLI Commands

The Asterisk CLI is a feature implemented in the main application.  Modules may
register additional CLI commands.

\subsection ArchInterfaceChannelDrivers Channel Drivers

The Asterisk channel driver interface is the most complex and most important
interface available.  The Asterisk channel API provides the telephony protocol
abstraction which allows all other Asterisk features to work independently of
the telephony protocol in use.

The specific interface that channel drivers implement is the ast_channel_tech
interface.  A channel driver must implement functions that perform various
call signaling tasks.  For example, they must implement a method for initiating
a call and hanging up a call.  The ast_channel data structure is the abstract
channel data structure.  Each ast_channel instance has an associated
ast_channel_tech which identifies the channel type.  An ast_channel instance
represents one leg of a call (a connection between Asterisk and an endpoint).

Channel drivers typically live in the <code>channels/</code> directory in the
source tree.

For a list of channel driver implementations, see \ref channel_drivers.

For additional information on the channel API, see
<code>include/asterisk/channel.h</code>.

For additional implementation details regarding the core ast_channel API, see
<code>main/channel.c</code>.

\subsection ArchInterfaceBridge Bridging Technologies

Bridging is the operation which connects two or more channels together.  A simple
two channel bridge is a normal A to B phone call, while a multi-party bridge would
be something like a 3-way call or a full conference call.

The bridging API allows modules to register bridging technologies.  An implementation
of a bridging technology knows how to take two (or optionally more) channels and
connect them together.  Exactly how this happens is up to the implementation.

This interface is used such that the code that needs to pass audio between channels
doesn't need to know how it is done.  Underneath, the conferencing may be done in
the kernel (via DAHDI), via software methods inside of Asterisk, or could be done
in hardware in the future if someone implemented a module to do so.

At the time of this writing, the bridging API is still relatively new, so it is
not used everywhere that bridging operations are performed.  The ConfBridge dialplan
application is a new conferencing application which has been implemented on top of
this bridging API.

Bridging technology modules typically live in the <code>bridges/</code> directory
in the source tree.

For a list of bridge technology implementations, see \ref bridges.

For additional information on the bridging API, see
\arg <code>include/asterisk/bridge.h</code>
\arg <code>include/asterisk/bridge_technology.h</code>
\arg <code>include/asterisk/bridge_channel.h</code>
\arg <code>include/asterisk/bridge_features.h</code>
\arg <code>include/asterisk/bridge_after.h</code>

For additional implementation details regarding the core bridging API, see
<code>main/bridge.c</code> and <code>main/bridge_channel.c</code>.

\subsection ArchInterfaceCDR Call Detail Record (CDR) Handlers

The Asterisk core implements functionality for keeping records of calls.  These
records are built while calls are processed and live in data structures.  At the
end of the call, these data structures are released.  Before the records are thrown
away, they are passed in to all of the registered CDR handlers.  These handlers may
write out the records to a file, post them to a database, etc.

CDR modules typically live in the <code>cdr</code> directory in the source tree.

For a list of CDR handlers, see \ref cdr_drivers.

For additional information on the CDR API, see
<code>include/asterisk/cdr.h</code>.

For additional implementation details regarding CDR handling, see
<code>main/cdr.c</code>.

\subsection ArchInterfaceCEL Call Event Logging (CEL) Handlers

The Asterisk core includes a generic event system that allows Asterisk components
to report events that can be subscribed to by other parts of the system.  One of
the things built on this event system is Call Event Logging (CEL).

CEL is similar to CDR in that they are both for tracking call history.  While CDR
records are typically have a one record to one call relationship, CEL events are
many events to one call.  The CEL modules look very similar to CDR modules.

CEL modules typically live in the <code>cel/</code> directory in the source tree.

For a list of CEL handlers, see cel_drivers.

For additional information about the CEL API, see
<code>include/asterisk/cel.h</code>.

For additional implementation details for the CEL API, see <code>main/cel.c</code>.

\subsection ArchInterfaceDialplanApps Dialplan Applications

Dialplan applications implement features that interact with calls that can be
executed from the Asterisk dialplan.  For example, in <code>extensions.conf</code>:

<code>exten => 123,1,NoOp()</code>

In this case, NoOp is the application.  Of course, NoOp doesn't actually do
anything.

These applications use a %number of APIs available in Asterisk to interact with
the channel.  One of the most important tasks of an application is to continuously
read audio from the channel, and also write audio back to the channel.  The details
of how this is done is usually hidden behind an API call used to play a file or wait
for digits to be pressed by a caller.

In addition to interacting with the channel that originally executed the application,
dialplan applications sometimes also create additional outbound channels.
For example, the Dial() application creates an outbound channel and bridges it to the
inbound channel.  Further discussion about the functionality of applications will be
discussed in detailed use cases.

Dialplan applications are typically found in the <code>apps/</code> directory in
the source tree.

For a list of dialplan applications, see \ref applications.

For details on the API used to register an application with the Asterisk core, see
<code>include/asterisk/pbx.h</code>.

\subsection ArchInterfaceDialplanFuncs Dialplan Functions

As the name suggests, dialplan functions, like dialplan applications, are primarily
used from the Asterisk dialplan.  Functions are used mostly in the same way that
variables are used in the dialplan.  They provide a read and/or write interface, with
optional arguments.  While they behave similarly to variables, they storage and
retrieval of a value is more complex than a simple variable with a text value.

For example, the <code>CHANNEL()</code> dialplan function allows you to access
data on the current channel.

<code>exten => 123,1,NoOp(This channel has the name: ${CHANNEL(name)})</code>

Dialplan functions are typically found in the <code>funcs/</code> directory in
the source tree.

For a list of dialplan function implementations, see \ref functions.

For details on the API used to register a dialplan function with the Asterisk core,
see <code>include/asterisk/pbx.h</code>.

\subsection ArchInterfaceRTP RTP Engines

The Asterisk core provides an API for handling RTP streams.  However, the actual
handling of these streams is done by modules that implement the RTP engine interface.
Implementations of an RTP engine typically live in the <code>res/</code> directory
of the source tree, and have a <code>res_rtp_</code> prefix in their name.

\subsection ArchInterfaceTiming Timing Interfaces

The Asterisk core implements an API that can be used by components that need access
to timing services.  For example, a timer is used to send parts of an audio file at
proper intervals when playing back a %sound file to a caller.  The API relies on
timing interface implementations to provide a source for reliable timing.

Timing interface implementations are typically found in the <code>res/</code>
subdirectory of the source tree.

For a list of timing interface implementations, see \ref timing_interfaces.

For additional information on the timing API, see <code>include/asterisk/timing.h</code>.

For additional implementation details for the timing API, see <code>main/timing.c</code>.


\section ArchThreadingModel Asterisk Threading Model

Asterisk is a very heavily multi threaded application.  It uses the POSIX threads API
to manage threads and related services such as locking.  Almost all of the Asterisk code
that interacts with pthreads does so by going through a set of wrappers used for
debugging and code reduction.

Threads in Asterisk can be classified as one of the following types:

 - Channel threads (sometimes referred to as PBX threads)
 - Network Monitor threads
 - Service connection threads
 - Other threads

\subsection ArchChannelThreads Channel Threads

A channel is a fundamental concept in Asterisk.  Channels are either inbound
or outbound.  An inbound channel is created when a call comes in to the Asterisk
system.  These channels are the ones that execute the Asterisk dialplan.  A thread
is created for every channel that executes the dialplan.  These threads are referred
to as a channel thread.  They are sometimes also referred to as a PBX thread, since
one of the primary tasks of the thread is to execute the Asterisk dialplan for an
inbound call.

A channel thread starts out by only being responsible for a single Asterisk channel.
However, there are cases where a second channel may also live in a channel thread.
When an inbound channel executes an application such as <code>Dial()</code>, an
outbound channel is created and bridged to the inbound channel once it answers.

Dialplan applications always execute in the context of a channel thread.  Dialplan
functions almost always do, as well.  However, it is possible to read and write
dialplan functions from an asynchronous interface such as the Asterisk CLI or the
manager interface (AMI).  However, it is still always the channel thread that is
the owner of the ast_channel data structure.

\subsection ArchMonitorThreads Network Monitor Threads

Network monitor threads exist in almost every major channel driver in Asterisk.
They are responsible for monitoring whatever network they are connected to (whether
that is an IP network, the PSTN, etc.) and monitor for incoming calls or other types
of incoming %requests.  They handle the initial connection setup steps such as
authentication and dialed %number validation.  Finally, once the call setup has been
completed, the monitor threads will create an instance of an Asterisk channel
(ast_channel), and start a channel thread to handle the call for the rest of its
lifetime.

\subsection ArchServiceThreads Service Connection Threads

There are a %number of TCP based services that use threads, as well.  Some examples
include SIP and the AMI.  In these cases, threads are used to handle each TCP
connection.

The Asterisk CLI also operates in a similar manner.  However, instead of TCP, the
Asterisk CLI operates using connections to a UNIX %domain socket.

\subsection ArchOtherThreads Other Threads

There are other miscellaneous threads throughout the system that perform a specific task.
For example, the event API (include/asterisk/event.h) uses a thread internally
(main/event.c) to handle asynchronous event dispatching.  The devicestate API
(include/asterisk/devicestate.h) uses a thread internally (main/devicestate.c)
to asynchronously process device state changes.


\section ArchConcepts Other Architecture Concepts

This section covers some other important Asterisk architecture concepts.

\subsection ArchConceptBridging Channel Bridging

As previously mentioned when discussing the bridging technology interface
(\ref ArchInterfaceBridge), bridging is the act of connecting one or more channel
together so that they may pass audio between each other.  However, it was also
mentioned that most of the code in Asterisk that does bridging today does not use
this new bridging infrastructure.  So, this section discusses the legacy bridging
functionality that is used by the <code>Dial()</code> and <code>Queue()</code>
applications.

When one of these applications decides it would like to bridge two channels together,
it does so by executing the ast_channel_bridge() API call.  From there, there are
two types of bridges that may occur.

 -# <b>Generic Bridge:</b> A generic bridge (ast_generic_bridge()) is a bridging
    method that works regardless of what channel technologies are in use.  It passes
    all audio and signaling through the Asterisk abstract channel and frame interfaces
    so that they can be communicated between channel drivers of any type.  While this
    is the most flexible, it is also the least efficient bridging method due to the
    levels of abstraction necessary.
 -# <b>Native Bridge:</b> Channel drivers have the option of implementing their own
    bridging functionality.  Specifically, this means to implement the bridge callback
    in the ast_channel_tech structure.  If two channels of the same type are bridged,
    a native bridge method is available, and Asterisk does not have a reason to force
    the call to stay in the core of Asterisk, then the native bridge function will be
    invoked.  This allows channel drivers to take advantage of the fact that the
    channels are the same type to optimize bridge processing.  In the case of a DAHDI
    channel, this may mean that the channels are bridged natively on hardware.  In the
    case of SIP, this means that Asterisk can direct the audio to flow between the
    endpoints and only require the signaling to continue to flow through Asterisk.


\section ArchCodeFlows Code Flow Examples

Now that there has been discussion about the various components that make up Asterisk,
this section goes through examples to demonstrate how these components work together
to provide useful functionality.

\subsection ArchCodeFlowPlayback SIP Call to File Playback

This example consists of a call that comes in to Asterisk via the SIP protocol.
Asterisk accepts this call, plays back a %sound file to the caller, and then hangs up.

Example dialplan:

<code>exten => 5551212,1,Answer()</code><br/>
<code>exten => 5551212,n,Playback(demo-congrats)</code><br/>
<code>exten => 5551212,n,Hangup()</code><br/>

 -# <b>Call Setup:</b> An incoming SIP INVITE begins this scenario.  It is received by
    the SIP channel driver (chan_sip.c).  Specifically, the monitor thread in chan_sip
    is responsible for handling this incoming request.  Further, the monitor thread
    is responsible for completing any handshake necessary to complete the call setup
    process.
 -# <b>Accept Call:</b> Once the SIP channel driver has completed the call setup process,
    it accepts the call and initiates the call handling process in Asterisk.  To do so,
    it must allocate an instance of an abstract channel (ast_channel) using the
    ast_channel_alloc() API call.  This instance of an ast_channel will be referred to
    as a SIP channel.  The SIP channel driver will take care of SIP specific channel
    initialization.  Once the channel has been created and initialized, a channel thread
    is created to handle the call (ast_pbx_start()).
 -# <b>Run the Dialplan:</b>: The main loop that runs in the channel thread is the code
    responsible for looking for the proper extension and then executing it.  This loop
    lives in ast_pbx_run() in main/pbx.c.
 -# <b>Answer the Call:</b>: Once the dialplan is being executed, the first application
    that is executed is <code>Answer()</code>.  This application is a built in
    application that is defined in main/pbx.c.  The <code>Answer()</code> application
    code simply executes the ast_answer() API call.  This API call operates on an
    ast_channel.  It handles generic ast_channel hangup processing, as well as executes
    the answer callback function defined in the associated ast_channel_tech for the
    active channel.  In this case, the sip_answer() function in chan_sip.c will get
    executed to handle the SIP specific operations required to answer a call.
 -# <b>Play the File:</b> The next step of the dialplan says to play back a %sound file
    to the caller.  The <code>Playback()</code> application will be executed.
    The code for this application is in apps/app_playback.c.  The code in the application
    is pretty simple.  It does argument handling and uses API calls to play back the
    file, ast_streamfile(), ast_waitstream(), and ast_stopstream(), which set up file
    playback, wait for the file to finish playing, and then free up resources.  Some
    of the important operations of these API calls are described in steps here:
    -# <b>Open a File:</b> The file format API is responsible for opening the %sound file.
       It will start by looking for a file that is encoded in the same format that the
       channel is expecting to receive audio in.  If that is not possible, it will find
       another type of file that can be translated into the codec that the channel is
       expecting.  Once a file is found, the appropriate file format interface is invoked
       to handle reading the file and turning it into internal Asterisk audio frames.
    -# <b>Set up Translation:</b> If the encoding of the audio data in the file does not
       match what the channel is expecting, the file API will use the codec translation
       API to set up a translation path.  The translate API will invoke the appropriate
       codec translation interface(s) to get from the source to the destination format
       in the most efficient way available.
    -# <b>Feed Audio to the Caller:</b> The file API will invoke the timer API to know
       how to send out audio frames from the file in proper intervals.  At the same time,
       Asterisk must also continuously service the incoming audio from the channel since
       it will continue to arrive in real time.  However, in this scenario, it will just
       get thrown away.
 -# <b>Hang up the Call:</b> Once the <code>Playback()</code> application has finished,
    the dialplan execution loop continues to the next step in the dialplan, which is
    <code>Hangup()</code>.  This operates in a very similar manner to <code>Answer()</code>
    in that it handles channel type agnostic hangup handling, and then calls down into
    the SIP channel interface to handle SIP specific hangup processing.  At this point,
    even if there were more steps in the dialplan, processing would stop since the channel
    has been hung up.  The channel thread will exit the dialplan processing loop and
    destroy the ast_channel data structure.

\subsection ArchCodeFlowBridge SIP to IAX2 Bridged Call

This example consists of a call that comes in to Asterisk via the SIP protocol.  Asterisk
then makes an outbound call via the IAX2 protocol.  When the far end over IAX2 answers,
the call is bridged.

Example dialplan:

<code>exten => 5551212,n,Dial(IAX2/mypeer)</code><br/>

 -# <b>Call Setup:</b> An incoming SIP INVITE begins this scenario.  It is received by
    the SIP channel driver (chan_sip.c).  Specifically, the monitor thread in chan_sip
    is responsible for handling this incoming request.  Further, the monitor thread
    is responsible for completing any handshake necessary to complete the call setup
    process.
 -# <b>Accept Call:</b> Once the SIP channel driver has completed the call setup process,
    it accepts the call and initiates the call handling process in Asterisk.  To do so,
    it must allocate an instance of an abstract channel (ast_channel) using the
    ast_channel_alloc() API call.  This instance of an ast_channel will be referred to
    as a SIP channel.  The SIP channel driver will take care of SIP specific channel
    initialization.  Once the channel has been created and initialized, a channel thread
    is created to handle the call (ast_pbx_start()).
 -# <b>Run the Dialplan:</b>: The main loop that runs in the channel thread is the code
    responsible for looking for the proper extension and then executing it.  This loop
    lives in ast_pbx_run() in main/pbx.c.
 -# <b>Execute Dial()</b>: The only step in this dialplan is to execute the
    <code>Dial()</code> application.
    -# <b>Create an Outbound Channel:</b> The <code>Dial()</code> application needs to
       create an outbound ast_channel.  It does this by first using the ast_request()
       API call to request a channel called <code>IAX2/mypeer</code>.  This API call
       is a part of the core channel API (include/asterisk/channel.h).  It will find
       a channel driver of type <code>IAX2</code> and then execute the request callback
       in the appropriate ast_channel_tech interface.  In this case, it is iax2_request()
       in channels/chan_iax2.c.  This asks the IAX2 channel driver to allocate an
       ast_channel of type IAX2 and initialize it.  The <code>Dial()</code> application
       will then execute the ast_call() API call for this new ast_channel.  This will
       call into the call callback of the ast_channel_tech, iax2_call(), which requests
       that the IAX2 channel driver initiate the outbound call.
    -# <b>Wait for Answer:</b> At this point, the Dial() application waits for the
       outbound channel to answer the call.  While it does this, it must continue to
       service the incoming audio on both the inbound and outbound channels.  The loop
       that does this is very similar to every other channel servicing loop in Asterisk.
       The core features of a channel servicing loop include ast_waitfor() to wait for
       frames on a channel, and then ast_read() on a channel once frames are available.
    -# <b>Handle Answer:</b> Once the far end answers the call, the <code>Dial()</code>
       application will communicate this back to the inbound SIP channel.  It does this
       by calling the ast_answer() core channel API call.
    -# <b>Make Channels Compatible:</b> Before the two ends of the call can be connected,
       Asterisk must make them compatible to talk to each other.  Specifically, the two
       channels may be sending and expecting to receive audio in a different format than
       the other channel.  The API call ast_channel_make_compatible() sets up translation
       paths for each channel by instantiating codec translators as necessary.
    -# <b>Bridge the Channels:</b> Now that both the inbound and outbound channels are
       fully established, they can be connected together.  This connection between the
       two channels so that they can pass audio and signaling back and forth is referred
       to as a bridge.  The API call that handles the bridge is ast_channel_bridge().
       In this case, the main loop of the bridge is a generic bridge, ast_generic_bridge(),
       which is the type of bridge that works regardless of the two channel types.  A
       generic bridge will almost always be used if the two channels are not of the same
       type.  The core functionality of a bridge loop is ast_waitfor() on both channels.
       Then, when frames arrive on a channel, they are read using ast_read().  After reading
       a frame, they are written to the other channel using ast_write().
    -# <b>Breaking the Bridge</b>: This bridge will continue until some event occurs that
       causes the bridge to be broken, and control to be returned back down to the
       <code>Dial()</code> application.  For example, if one side of the call hangs up,
       the bridge will stop.
 -# <b>Hanging Up:</b>: After the bridge stops, control will return to the
    <code>Dial()</code> application.  The application owns the outbound channel since
    that is where it was created.  So, the outbound IAX2 channel will be destroyed
    before <code>Dial()</code> is complete.  Destroying the channel is done by using
    the ast_hangup() API call.  The application will return back to the dialplan
    processing loop.  From there, the loop will see that there is nothing else to
    execute, so it will hangup on the inbound channel as well using the ast_hangup()
    function.  ast_hangup() performs a number of channel type independent hangup
    tasks, but also executes the hangup callback of ast_channel_tech (sip_hangup()).
    Finally, the channel thread exits.


\section ArchDataStructures Asterisk Data Structures

Asterisk provides generic implementations of a number of data structures.

\subsection ArchAstobj2 Astobj2

Astobj2 stands for the Asterisk Object model, version 2.  The API is defined in
include/asterisk/astobj2.h.  Some internal implementation details for astobj2 can
be found in main/astobj2.c.  There is a version 1, and it still exists in the
source tree.  However, it is considered deprecated.

Astobj2 provides reference counted object handling.  It also provides a container
interface for astobj2 objects.  The container provided is a hash table.

See the astobj2 API for more details about how to use it.  Examples can be found
all over the code base.

\subsection ArchLinkedLists Linked Lists

Asterisk provides a set of macros for handling linked lists.  They are defined in
include/asterisk/linkedlists.h.

\subsection ArchDLinkedLists Doubly Linked Lists

Asterisk provides a set of macros for handling doubly linked lists, as well.  They
are defined in include/asterisk/dlinkedlists.h.

\subsection ArchHeap Heap

Asterisk provides an implementation of the max heap data structure.  The API is defined
in include/asterisk/heap.h.  The internal implementation details can be found in
main/heap.c.


\section ArchDebugging Asterisk Debugging Tools

Asterisk includes a %number of built in debugging tools to help in diagnosing common
types of problems.

\subsection ArchThreadDebugging Thread Debugging

Asterisk keeps track of a list of all active threads on the system.  A list of threads
can be viewed from the Asterisk CLI by running the command
<code>core show threads</code>.

Asterisk has a compile time option called <code>DEBUG_THREADS</code>.  When this is on,
the pthread wrapper API in Asterisk keeps track of additional information related to
threads and locks to aid in debugging.  In addition to just keeping a list of threads,
Asterisk also maintains information about every lock that is currently held by any
thread on the system.  It also knows when a thread is blocking while attempting to
acquire a lock.  All of this information is extremely useful when debugging a deadlock.
This data can be acquired from the Asterisk CLI by running the
<code>core show locks</code> CLI command.

The definitions of these wrappers can be found in <code>include/asterisk/lock.h</code>
and <code>include/asterisk/utils.h</code>.  Most of the implementation details can be
found in <code>main/utils.c</code>.

\subsection ArchMemoryDebugging Memory debugging

Dynamic memory management in Asterisk is handled through a %number of wrappers defined
in <code>include/asterisk/utils.h</code>.  By default, all of these wrappers use the
standard C library malloc(), free(), etc. functions.  However, if Asterisk is compiled
with the MALLOC_DEBUG option enabled, additional memory debugging is included.

The Asterisk memory debugging system provides the following features:

 - Track all current allocations including their size and the file, function, and line
   %number where they were initiated.
 - When releasing memory, do some basic fence checking to see if anything wrote into the
   few bytes immediately surrounding an allocation.
 - Get notified when attempting to free invalid memory.

A %number of CLI commands are provided to access data on the current set of memory
allocations.  Those are:

 - <code>memory show summary</code>
 - <code>memory show allocations</code>

The implementation of this memory debugging system can be found in
<code>main/astmm.c</code>.


<hr>
Return to the \ref ArchTOC
 */
