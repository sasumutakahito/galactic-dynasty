all:GalacticDynasty

main.o:
	gcc -c main.cpp

interbbs2.o:
	gcc -c interbbs2.cpp

GalacticDynasty: main.o interbbs2.o
	g++ -o GalacticDynasty main.o interbbs2.o  -lODoors -lsqlite3
