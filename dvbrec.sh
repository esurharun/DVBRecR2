#!/bin/bash
DIRECTORY=`dirname $0`
cd $DIRECTORY
scala -cp lib/red5.jar:lib/slf4j-api-1.4.3.jar:lib/logback-classic-0.9.8.jar:lib/logback-core-0.9.8.jar:lib/mina-core-1.1.6.jar:lib/xercesImpl-2.9.0.jar /opt/DVBRecR2/target/scala-2.9.1/dvbrec_2.9.1-0.0.1.jar $1 
