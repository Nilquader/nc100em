#!/bin/sh
#
# makememcard - make a 1 meg pcmcia card image in ~/nc100

if [ "$NC100EM_HOME" = "" ]; then
  if [ "$HOME" = "" ]; then 
    NC100EM_HOME=.
  else
    NC100EM_HOME=$HOME/nc100
  fi
fi
if [ ! -d $NC100EM_HOME ]; then
  mkdir $NC100EM_HOME
fi
if [ -f $NC100EM_HOME/nc100.card ]; then
  echo "makememcard: card already exists in $NC100EM_HOME, aborting!"
  exit 1
else
  echo 'makememcard: making 1024k nc100.card...'
  dd if=/dev/zero of=$NC100EM_HOME/nc100.card bs=1k count=1024
  echo 'done.'
fi
