#!/bin/sh
# Twice because of some indent bug which changes a file back and forth
indent -bap -bc -bfda -bfde -bl -bli2 -bls -cbi2 -cdb -cdw -nce -ci2 -cli2 -i2 -ip2 -kr -l79 -lp -ncs -nhnl -pcs -pi2 -ppi2 -prs -psl -saf -sai -saw -sc -sob -ts8 $*
indent -bap -bc -bfda -bfde -bl -bli2 -bls -cbi2 -cdb -cdw -nce -ci2 -cli2 -i2 -ip2 -kr -l79 -lp -ncs -nhnl -pcs -pi2 -ppi2 -prs -psl -saf -sai -saw -sc -sob -ts8 $*
git add $*
