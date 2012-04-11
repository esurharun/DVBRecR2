#!/bin/sh
  exec scala "$0" "$@"
!#


import java.util.Properties
import java.io.FileInputStream

object DvbRec {

	def main(args: Array[String]): Unit = {

		if (args.length == 0) {
			Console.println("You should give a configuration file")		
			exit(0)
		}

		val props = new Properties()

		try {
			props.load(new FileInputStream(args(0)))
		} catch  {
	
			case e: Exception => {
				Console.println("Cannot read: %s".format(args(0)))	
				exit(0)
			}
		}


		val OPT_CHANNEL_ID = props.getProperty("CHANNELID")
		val OPT_CHANNEL_NAME = props.getProperty("CHANNELNAME")
		val OPT_RECPATH = props.getProperty("RECPATH")
		val OPT_FREQUENCY = props.getProperty("FREQUENCY")
		val OPT_POL = props.getProperty("POL")
		val OPT_SYMBOLRATE = props.getProperty("SYMBOLRATE")
		val OPT_AUDIOPID = props.getProperty("AUDIOPID")
		val OPT_VIDEOPID = props.getProperty("VIDEOPID")
		val OPT_DISEQC = props.getProperty("DISEQC")
		val OPT_FLV_ENCODING = props.getProperty("FLV_ENCODING")
		val OPT_FLV_RECPATH = props.getProperty("FLV_RECPATH")		


	//	Console.println("Hello %s".format(args(0)));

	}

}


DvbRec.main(args)
