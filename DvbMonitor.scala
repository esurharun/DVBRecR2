
 
// /usr/bin/dvbstream -c ${CHANNEL} -ps -f ${FREQUENCY} -p ${POL} -s ${SYMBOLRATE} -D ${DISEQC} -I 2 -o ${AUDIOPID} ${VIDEOPID}


import scala.actors.Actor
import Actor._
import java.lang.ProcessBuilder
import java.lang.Process
import java.io.InputStreamReader
import java.io.BufferedReader
import scala.util.control.Breaks._

class DvbMonitor(var channelId: Int,
		var frequency: String,
		var polarization: String,
		var symbolrate: String,
		var diseqc: String,
		var audiopid: Int,
		var videopid: Int) extends Actor {

	val DVBSTREAM_CMD = "/usr/bin/dvbstream -c %s -f %s -p %s -s %s -D %s -I 2 -ps -o %s %s" 

	val MSG_LOCKED = "FE_STATUS: FE_HAS_LOCK FE_HAS_CARRIER FE_HAS_VITERBI FE_HAS_SYNC"
	val MSG_FAILED = "Not able to lock to the signal on the given frequency"
	val MSG_BUSY = "Device or resource busy"
	val MSG_VIDEOSTREAM = "Videostream"
	var locked = false

	var transcoders: List[Transcoder] =  List[Transcoder]()

	def act() {
		
		val cmd_str = DVBSTREAM_CMD.format(channelId,frequency,polarization,symbolrate,diseqc,audiopid,videopid)
		Console.println("%s".format(cmd_str))
		val cmd = cmd_str.split("\\s")
		var dvbsproc: Process = null
		var dvbsproc_stderr: BufferedReader = null


		while (!locked) {
	
			Console.println("Trying to launch stream monitor")

			dvbsproc = new ProcessBuilder( java.util.Arrays.asList(cmd: _*)).start()
			dvbsproc_stderr = new BufferedReader(new InputStreamReader(dvbsproc.getErrorStream))

			
			var pDone = false
			while (!pDone) {
					Thread.sleep(5)
					val r_line = dvbsproc_stderr.readLine()
					if (r_line != null) {

						if (r_line.indexOf(MSG_LOCKED) != -1) {
							Console.println("Successfully locked to: %s %s %s".format(frequency,polarization,symbolrate))
							
							
						}

						if (r_line.indexOf(MSG_VIDEOSTREAM) != -1) {

							Console.println("Stream started")

							locked = true
							pDone = true
						}

						if (r_line.indexOf(MSG_FAILED) != -1 || r_line.indexOf(MSG_BUSY) != -1) {
							Console.println("Failed to lock to frequency rates.. will try again")
							dvbsproc.destroy()
							Thread.sleep(5000)
							pDone = true

						}

						Console.println("[] => %s".format(r_line))
					} 
				
			 }
			

		}

		val dvbsproc_stdout = dvbsproc.getInputStream

		val readedBytes = new Array[Byte](1024)

		var readed: Int = 0
		while (true) {
			
			//Thread.sleep(1)

			// val e_line = dvbsproc_stderr.readLine()

			// if (e_line != null)
			// 	Console.println("[] => %s".format(e_line))

			//Console.println("Before read")
			val count = dvbsproc_stdout.read(readedBytes)
			//Console.println("After read")
			if (count != -1) {
			//	readed += count
			//	Console.println("Readed %s bytes..".format(readed))
				transcoders.foreach(transcoder => {
					transcoder.transproc.getOutputStream.write(readedBytes,0,count)
				})
			}


			


		}

	}



}

