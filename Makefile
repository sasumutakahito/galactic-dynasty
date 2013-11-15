all:GalacticDynasty

main.o:
	gcc -c main.cpp

interbbs.o:
	gcc -c interbbs.cpp

interbbs_jam_crc32.o:
	gcc -c interbbs_jam_crc32.cpp

interbbs_jam_lastread.o:
	gcc -c interbbs_jam_lastread.cpp
  
interbbs_jam_mbase.o:
	gcc -c interbbs_jam_mbase.cpp
  
interbbs_jam_message.o:
	gcc -c interbbs_jam_message.cpp
  
interbbs_jam_structrw.o:
	gcc -c interbbs_jam_structrw.cpp

interbbs_jam_subpack.o:
	gcc -c interbbs_jam_subpack.cpp

GalacticDynasty: main.o interbbs.o interbbs_jam_crc32.o interbbs_jam_lastread.o interbbs_jam_mbase.o interbbs_jam_message.o interbbs_jam_structrw.o interbbs_jam_subpack.o
	g++ -o GalacticDynasty main.o interbbs.o interbbs_jam_crc32.o interbbs_jam_lastread.o interbbs_jam_mbase.o interbbs_jam_message.o interbbs_jam_structrw.o interbbs_jam_subpack.o -lODoors -lsqlite3
