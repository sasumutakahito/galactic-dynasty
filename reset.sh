#!/bin/sh
cp users.db3.orig users.db3
cp msgs.db3.orig msgs.db3
cp interbbs.db3.orig interbbs.db3
if [ -e ibbs_scores.ans ]
then
	rm ibbs_scores.ans
fi

if [ -e scores.ans ]
then
        rm scores.ans
fi
