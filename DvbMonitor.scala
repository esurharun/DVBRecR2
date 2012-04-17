
 
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

		val bBuffer = ByteBuffer.allocate(1024 * 10)
		val dvbsproc_stdout = java.nio.channels.Channels.newChannel(dvbsproc.getInputStream)

		val readedBytes = new Array[Byte](1024000)

		var readed: Int = 0
		var readTime: Long = 0
		var writeTime: Long = 0
		var loopCount: Long = 0
		var readCount: Long = 0
		while (true) {
			
			
			val startToReadTime = System.nanoTime()
			bBuffer.clear
			val count = dvbsproc_stdout.read(bBuffer)
			readTime += (System.nanoTime()-startToReadTime)/1000000
			readCount += count 


			val dataflowing = (count != -1)

			if (dataflowing) {
				
				val startToWriteTime = System.nanoTime()

				bBuffer.flip()
					
				transcoders.foreach(transcoder => {
					transcoder.writeChannel.write(bBuffer)
					bBuffer.rewind
					 
				})
				writeTime += (System.nanoTime()-startToWriteTime)/1000000
			} 

			 


			
			loopCount += 1

			if (loopCount % 5000 == 0)
			{
				println("Read time avg: %3.2f Write time avg: %3.2f Read c. avg: %d ".format((readTime.toDouble/loopCount.toDouble), (writeTime.toDouble/loopCount.toDouble), readCount/loopCount))
				loopCount = 0
				readTime = 0
				writeTime = 0
				readCount = 0
			}

			


		}

	}



}

