
 
// /usr/bin/dvbstream -c ${CHANNEL} -ps -f ${FREQUENCY} -p ${POL} -s ${SYMBOLRATE} -D ${DISEQC} -I 2 -o ${AUDIOPID} ${VIDEOPID}


import scala.actors.Actor
import Actor._
import java.lang.ProcessBuilder
import java.lang.Process
import java.io.InputStreamReader
import java.io.BufferedReader
import scala.util.control.Breaks._
import java.nio._
import java.nio.channels._

class DvbMonitor(var channelId: Int,
		var frequency: String,
		var polarization: String,
		var symbolrate: String,
		var diseqc: String,
		var audiopid: Int,
		var videopid: Int,
                var first_interval: Int,
                var general_interval: Int) extends Actor {

	val DVBSTREAM_CMD = "/usr/bin/dvbstream_modified -c %s -f %s -p %s -s %s -D %s -I 2 -ps -o:%s -n %s,%s %s %s" 

	val MSG_LOCKED = "FE_STATUS: FE_HAS_LOCK FE_HAS_CARRIER FE_HAS_VITERBI FE_HAS_SYNC"
	val MSG_FAILED = "Not able to lock to the signal on the given frequency"
	val MSG_BUSY = "Device or resource busy"
	val MSG_VIDEOSTREAM = "Videostream"
	var locked = false

	var waiting_to_transcode: List[String] =  List[String]()

	def act() {
	
                var st_time = java.util.Calendar.getInstance()
		var dvbsproc: Process = null
		var dvbsproc_stderr: BufferedReader = null


		while (!locked) {
	
			Console.println("Trying to launch stream monitor")

                        val diff = (java.util.Calendar.getInstance().getTimeInMillis-st_time.getTimeInMillis)/1000

                        val cmd_str = DVBSTREAM_CMD.format(channelId,frequency,polarization,symbolrate,diseqc,
                                   "/tmp/dvbrec.ch%s.dump.ts".format(channelId), first_interval-diff, general_interval,
                                  audiopid,videopid)
               		Console.println("%s".format(cmd_str))
	                val cmd = cmd_str.split("\\s")


			dvbsproc = new ProcessBuilder( java.util.Arrays.asList(cmd: _*)).start()
			dvbsproc_stderr = new BufferedReader(new InputStreamReader(dvbsproc.getErrorStream))

			
			var pFailed = false
			while (!pFailed) {
					Thread.sleep(200)
					val r_line = dvbsproc_stderr.readLine()
					if (r_line != null) {

                                                //Finished: /tmp/my.ts.1335270726760-1335270735757 
                                                //Open file /tmp/my.ts.1335270735757.onrec
			

                                                if (r_line.indexOf("Finished:") == 0) {
                                                        val file_name = r_line.split(" ")(1) 
                                                        waiting_to_transcode = waiting_to_transcode ++: List(file_name)
                                                        Console.println("%s added to transcode queue".format(file_name))
                                                } else if (r_line.indexOf("Open file") == 0) {
                                                        val file_name = r_line.split(" ")(2)
                                                        Console.println("%s started to dump".format(file_name))
                                                } else 	if (r_line.indexOf(MSG_LOCKED) != -1) {
							Console.println("Successfully locked to: %s %s %s".format(frequency,polarization,symbolrate))
						} else if (r_line.indexOf(MSG_VIDEOSTREAM) != -1) {

							Console.println("Stream started")

							locked = true
						} else if (r_line.indexOf(MSG_FAILED) != -1 || r_line.indexOf(MSG_BUSY) != -1) {
							Console.println("Failed to lock to frequency rates.. will try again")
							Thread.sleep(5000)
							pFailed = true

						} else 
        						Console.println("[] => %s".format(r_line))
					} 
				
			 }
			

		}

	}



}

