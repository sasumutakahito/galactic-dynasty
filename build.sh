#!/bin/bash

if [ -ne odoors ]
then
    git clone https://github.com/apamment/odoors
fi

cd odoors
make

cd ..

gcc -c main.c -o main.o -I./odoors/
gcc -c interbbs2.c -o interbbs2.o 

gcc -o GalacticDynasty main.o interbbs2.o odoors/libs-`uname -s`/libODoors.a -lsqlite3
