# Galactic Dynasty

Win32 [![Build Status](http://magickabbs.com:8080/buildStatus/icon?job=GalacticDynasty-Win32)](http://magickabbs.com:8080/job/GalacticDynasty-Win32)
Linux [![Build Status](http://magickabbs.com:8080/buildStatus/icon?job=GalacticDynasty-Linux)](http://magickabbs.com:8080/job/GalacticDynasty-Linux)

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

The galactic.ini file is where you configure the game. So far only two options exist:

*Turns Per Day* This is the number of turns each player can play in a day

*Turns in Protection* This is the number of turns a player will be protected from attack.

### InterBBS

InterBBS is configured using BBS.CFG, if it exists the game will function in interbbs mode, if not it will function in Single BBS mode.

The format of BBS.CFG can be found in EXAMPLE.CFG included in the distribution.

Basically, you define settings for your node, then settings for each link.

For your node:

 * *SystemName* Your BBS Name
 * *LeagueNo* Your League Number
 * *NodeNo* Your Node Number
 * *FileInbox* Your incoming file folder

For each Link

 * *LinkName* The Name of a Linked BBS
 * *LinkFileOutbox* The outbox for files for this link.

Files can either be sent directly to the link, or via other links. If a link receives a packet not destined to it, it will forward it to the outbox specified in it's config.

To create packets and import/forward packets, you must run 

    GalacticDynasty maintenance
    
Again, this must be run with the working directory as the Galactic Dynasty directory. 

Don't run maintenance more than once a minute as you will get file collisions. A good frequency to run maintenance would be once a day, but you can increase that if you would like a more responsive interbbs experience.

