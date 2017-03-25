# Galactic Dynasty

Build Status:

 * Win32 [![Build Status](https://build.magickabbs.com/buildStatus/icon?job=GalacticDynasty-Win32)](https://build.magickabbs.com/job/GalacticDynasty-Win32)
 * Linux [![Build Status](https://build.magickabbs.com/buildStatus/icon?job=GalacticDynasty-Linux)](https://build.magickabbs.com/job/GalacticDynasty-Linux)

Galactic Dynasty is a BBS Door Game for Windows and Linux, similar to Solar Realms Elite
but much simpler and with InterBBS support.

The idea is to start with a simple base and see where we can go from there in making a 
new, interesting and fun door game.

### Installation

On windows, it's easiest to grab the latest binary from the releases page and unzip it somewhere.

On Linux, you will need to build from source. Either grab the latest source zip file, or clone from the repo.

Make sure you have make, a compiler, git and sqlite3-dev installed, then run 

    chmod a+x build.sh
    ./build.sh

### Setup

##### Windows

You will need to call the game from a batch file that first sets the working directory to the Galactic Dynasty directory, then call the executable like this

    GalacticDynasty.exe -D C:\Path\To\door32.sys

You may need to include the socket:

    GalacticDynasty.exe -D C:\Path\To\door32.sys -SOCKET %SOCKET%

Where %SOCKET% is the socket number passed from your BBS.


##### Linux

Linux is similar to Windows, except requires STDIO redirection. Use dorinfo1.def as your drop file, or door.sys.

You will need to write a shell script that first changes the working directory then calls Galactic Dynasty with the -D switch for the drop file.

### Configuring

The galactic.ini file is where you configure the game.

Under the [main] section:

*Turns Per Day* This is the number of turns each player can play in a day

*Turns in Protection* This is the number of turns a player will be protected from attack.

Under the [InterBBS] section

*Enabled* Either False or True if InterBBS mode is to be enabled.

*System Name* Your system name

*League Number* The league number the game is a part of

*Node Number* Your node number within the league

*File Inbox* Where to look for incoming .GAL files

*Default Outbox* Default outbox, used when LinkFileOutbox is not defined in BBS.CFG

### InterBBS

InterBBS is configured using BBS.CFG, if it exists the game will function in interbbs mode, if not it will function in Single BBS mode.

The format of BBS.CFG can be found in EXAMPLE.CFG included in the distribution.

For each Link
 * *LinkNodeNumber* The node number of the link (MUST BE FIRST)
 * *LinkName* The Name of a Linked BBS
 * *LinkFileOutbox* The outbox for files for this link. (Optional)

Files can either be sent directly to the link, or via other links. If a link receives a packet not destined to it, it will forward it to the outbox specified in it's config.

To create packets and import/forward packets, you must run 

    GalacticDynasty maintenance
    
Again, this must be run with the working directory as the Galactic Dynasty directory. 

Don't run maintenance more than once a minute as you will get file collisions. A good frequency to run maintenance would be once a day, but you can increase that if you would like a more responsive interbbs experience.

### Score Files

Score files are generated in ascii and ansi formats. The headers and footers can be customized to suit your BBS.