Welcome to automation-ng!

This is not an official Google product.

Setting up your own automation involves a lot of steps, but hopefully
isn't too hard. 

automation makes heavy use of the Google C++ flag library.  In this
document, you will see us frequently refer to "FLAGS_xxx" - when this
happens, we are referring to the value controlled by setting the command
line flag --xxx

==== PRE-PREREQUISITES ====

First, install the source.  If you're reading this file from your machine,
odds are you've done this already.  Otherwise, see about cloning
this git repository to your local machine.

==== PREREQUISITES ====

First, install the following packages:

  % apt-get install libpion-net-dev libboost-dev cmake liblog4cpp5-dev \
                libsqlite3-dev libssl-dev libboost-thread-dev \
                libboost-system-dev libboost-regex-dev sqlite3 git \
                libprotobuf-dev libjsoncpp-dev

If you're lucky, you may be able to just run 'make' at this point.

When this is done, you should have an automation binary present in the
build directory.  Install it to /usr/local/bin/ with cp:

  % cp automation /usr/local/bin/ 

==== SETTING UP THE DATABASE FOR FIRST USE ====

automation and the helper tool acmd will both CHECK-fail on startup
if the database doesn't exist.  To instruct it to create the database
instead, pass the flag dbinit:

  % ./automation --dbinit [--dbname /path/to/file/to/create/automation.db] 

If everything was successful, this will return silently. Note subsequent
calls to --dbinit will fail.  This exists as a safety mechanism - we never
create databases unless explicitly asked to.

We can now start the automation service, but we'll have to use some funky
flags the first time.

==== REQUIREMENTS, APIs, PLAYLISTS, AWESOMENESS ====

Automation is designed to pass time between any number of specified requirements.
Requirements can be scheduled to happen at specific times, and may also be marked
with a 'reboot' bit which indicates they should also happen immediately upon
startup of automation.  If the reboot behavior is not desired, it may be
disabled entirely by setting FLAGS_doinit to false (--doinit=false).

It may be desirable in many jurisdictions to play a legal station identification at
the top of every hour.  This is such a common requirement that it can be configured
implicity by automation, by setting the --implicit_legalid flag.  The implicit_legalid
requirement is also configured with the reboot bit; this is especially desirable
in any sort of unforseen error state where we are crashing regularly, but still able
to play legal IDs.

The 'legalid' requirement is satisfied by playing one track from the legalid
playlist (defined by FLAGS_legalid default value 'legalid') that is up to
FLAGS_legalid_max_length seconds in duration.

Automation exposes critical internal state over its Web API.  Among other things,
our set of requirements ('a schedule') is exposed via this manner, as our the
playlists themselves.  Full documentation of the API can be found in apidocs.txt

In addition to requirements, automation keeps track of PlayableItems, which are
things that can be played.  Sets of PlayableItems are called Playlists.  Playlists
may have a 'weight' - automation's default behavior is to select a weighted
random playlist[1] and use that playlist for playing tracks until the next
requirement.

In the event a randomly selected playlist is exhausted, the behavior depends
on when the next requirement is due:
  1. If more than FLAGS_bumpercutoff of time is remaining, we select a new
     playlist and random with the original weighted heuristic.  It is possible
     that the same playlist will be selected again.
  2. If less than FLAGS_sleepcutoff of time is remaining, we will sleep for
     that long, silently.
  3. If some time X such that FLAGS_sleepcutoff < X < FLAGS_bumpercutoff remains,
     we attempt to pass time using bumpers.  If FLAGS_bumpers is empty (default),
     we will scan the entire PlayableItems set and attempt to play as few tracks
     as possible to bring us to the next deadline. Otherwise, we draw on the 
     playlist defined by FLAGS_bumpers to do the same thing.

==== COMMAND LINE FUN ====

automation ships with 'acmd' which can be used for several routine tasks,
including playlist maintenance.  Critically, it is also the only supported
mechanism of inserting PlayableItems into the database.

It is designed to be used with find:
  % find /path/to/content -type f -name \*.mp3 | ./acmd --command=load

This will find all files under /path/to/content that are named like MP3s.
It then passes it off to acmd in 'load' mode.  In this mode, it will look
up each item in PlayableItems and, if it's found, print (tab-delimited)
the ID and the filename back to its standard output. If it isn't found,
it will calculate the duration of the item, and assuming it is non-zero,
it will be inserted into PlayableItems and printed back to the user as
usual.

There are also a pair of commands, append and replace, used for setting
playlists to specific sets of PlayableItems.  'append' adds to existing
playlists, where 'replace' clears them first.  In this mode we take PlayableItemIDs,
one per line, on the standard input.  We ignore anything from the first space
character to the end of line; this lets us chain together with the load command.
In this mode, the affected playlist is FLAGS_playlist.

  % find /path/to/IDs -type f -name \*.mp3 | ./acmd --command=load | ./acmd --command=replace --playlist=legalid
  % find /path/to/content -type f -name \*.mp3 | ./acmd --command=load | ./acmd --command=append --playlist=funcontent

We can also mutate the weight of a playlist using --command=setup
  % ./acmd --command=setup --playlist=funcontent --weight=30

Please see automation --helpfull for more details on command-line flags, or apidocs.txt for
information on interacting with automation over our RESTful interface. 

Good luck!

==== Notes ====

[1] Assume a set of playlists, P0, P1, P2... with weights W0, W1, W2.
The probability of selecting playlist PX is WX/(sum of all weights)

