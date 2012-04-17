
import java.util.Properties
import java.io.FileInputStream
import scala.actors.Actor
import Actor._
import java.io._

class FileMover(val source:String, val target:String) extends Actor {
	
	def act() {
		
		println("Moving %s to %s".format(source,target))

		try {
			// val sFile = new java.io.File(source)
			// val tFile = new java.io.File(target)

			// val inStream = new FileInputStream(sFile)
			// val outStream = new FileOutputStream(tFile)

			// val buffer = new Array[Byte](1000000)

			// var length: Int = inStream.read(buffer)

			// while (length > 0) {
			// 	//Thread.sleep(5)
			// 	outStream.write(buffer,0,length)

			// 	length = inStream.read(buffer)
			// }

			// inStream.close
			// outStream.close


			val cmd = "mv %s %s".format(source,target) .split("\\s")
			val transproc = new ProcessBuilder( java.util.Arrays.asList(cmd: _*)).start()


			// var transproc_stderr: BufferedReader =  new BufferedReader(new InputStreamReader(transproc.getErrorStream))
			// var transproc_stdout: BufferedReader =  new BufferedReader(new InputStreamReader(transproc.getInputStream))

			// while (transproc_stderr.readLine() != null && 
			// 		transproc_stdout.readLine() != null )
			// 		Thread.sleep(10)

			transproc.waitFor()


			println("Successfully moved %s to %s".format(source,target))
			//sFile.delete
		} catch {
			case e: Exception => {
				println("Could not copied %s to %s".format(source,target))
			}
		}




	}

}

object DvbRec {

       


	def main(args: Array[String]): Unit = {

		if (args.length == 0) {
			println("You should give a configuration file")
			exit
		}

		var props = new Properties()

		try {
			props.load(new FileInputStream(args(0)))
		} catch  {

			case e: Exception => {
				println("Cannot read: %s".format(args(0)))
				exit
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
		val OPT_FLV_ENABLED = props.getProperty("FLV_ENABLED")
		val OPT_FLV_RECPATH = props.getProperty("FLV_RECPATH")		



		val dvbMonitor = new DvbMonitor(OPT_CHANNEL_ID.toInt,
										OPT_FREQUENCY,
										OPT_POL,
										OPT_SYMBOLRATE,
										OPT_DISEQC,
										OPT_AUDIOPID.toInt,
										OPT_VIDEOPID.toInt) 

		dvbMonitor.start()
		while (!dvbMonitor.locked)
			Thread.sleep(1000)
		println("Locked!!!")

		

		do {

			val encode_flv = OPT_FLV_ENABLED.trim.toLowerCase == "true"

			
			val tmp_mpg_loc = "/tmp/dvbrec.%s.%s.mpg".format(OPT_CHANNEL_ID,System.nanoTime)
			val tmp_flv_loc = "/tmp/dvbrec.%s.%s.flv".format(OPT_CHANNEL_ID,System.nanoTime)

			val MPEG_TRANSCODER = new Transcoder("ffmpeg -i pipe: -threads 4 -target pal-vcd -async 44100 -y %s".format(tmp_mpg_loc),
												 "MPEG_TRANSCODER")
			var FLV_TRANSCODER: Transcoder = null
			if (encode_flv) {
				FLV_TRANSCODER = new Transcoder("ffmpeg -i pipe: -y -threads 4 -acodec libfaac -ar 22500 -ab 96k -coder ac -sc_threshold 40 -vcodec libx264 -b 270k -minrate 270k -maxrate 270k -bufsize 2700k -cmp +chroma -partitions +parti4x4+partp8x8+partb8x8 -i_qfactor 0.71 -keyint_min 25 -b_strategy 1 -g 250 -s 352x288 %s".format(tmp_flv_loc),
					"FLV_TRANSCODER")
			}
			
			MPEG_TRANSCODER.start()

			if (encode_flv)
				FLV_TRANSCODER.start()

			while (MPEG_TRANSCODER.writeChannel == null)
		 		Thread.sleep(5)
		 	
		 	if (encode_flv)  
		 		while (FLV_TRANSCODER.writeChannel == null)
		 			Thread.sleep(5)

 			val start_time = java.util.Calendar.getInstance()


			dvbMonitor.transcoders = dvbMonitor.transcoders  :+ MPEG_TRANSCODER

			if (encode_flv)
				dvbMonitor.transcoders = dvbMonitor.transcoders :+ FLV_TRANSCODER

			try {

				import java.util.Calendar._
				import java.util.Calendar

				Thread.sleep(1000)
				while (true) {
					Thread.sleep(100)	
					val cur_time = java.util.Calendar.getInstance()
					
					val hour = cur_time.get(Calendar.HOUR_OF_DAY)
					val min = cur_time.get(Calendar.MINUTE)
					val sec = cur_time.get(Calendar.SECOND)

					if ((hour == 0 || hour % 2 == 0) &&
							min == 0 &&
							sec == 0)
							{
								println("Restarting transcode..")

								dvbMonitor.transcoders = List[Transcoder]()
								MPEG_TRANSCODER.terminate = true
								if (encode_flv)
									FLV_TRANSCODER.terminate = true
								
								val fileName = "-%s%02d%02d_%02d%02d%02d-%02d%02d%02d".format(
									start_time.get(Calendar.YEAR),
									start_time.get(Calendar.MONTH)+1,
									start_time.get(Calendar.DAY_OF_MONTH),
									start_time.get(Calendar.HOUR_OF_DAY),
									start_time.get(Calendar.MINUTE),
									start_time.get(Calendar.SECOND),
									hour,
									min,
									sec
									)


								new FileMover(tmp_mpg_loc,
									"%s/CH%s%s.mpg".format(OPT_RECPATH,OPT_CHANNEL_ID,fileName)).start()

								if (encode_flv) {
									(new Actor {
										def act() {
											import org.red5.io.flv.impl._
											import org.red5.io.flv.meta._
											import org.red5.io._
											
											val tFlvFile = new java.io.File(tmp_flv_loc)	

											println("Generating metadatas for %s".format(tmp_flv_loc))
											val flvReader = new FLVReader(tFlvFile,true);
		                        			val metaCache = new FileKeyFrameMetaCache();
		                        			metaCache.saveKeyFrameMeta(tFlvFile, flvReader.analyzeKeyFrames());

		                        			val newDirs = "/%s/%02d/%02d/".format(start_time.get(Calendar.YEAR),
		                        											start_time.get(Calendar.MONTH)+1,
		                        											start_time.get(Calendar.DAY_OF_MONTH))

		                        			val targetPath = "%s/%s".format(OPT_FLV_RECPATH, newDirs)

		                        			new File(targetPath).mkdirs

											new FileMover(tmp_flv_loc,"%s/%s%s.flv".format(targetPath,
												OPT_CHANNEL_NAME,
												fileName)).start
											new FileMover("%s.meta".format(tmp_flv_loc),
														  "%s/%s%s.flv.meta".format(targetPath,
														  	OPT_CHANNEL_NAME,
															  fileName)).start
										}
									}).start()
								}
								
								throw new Exception("")
								

							}


				}
				
			} catch {
				case e: Exception => {
					
				}
			}
		} while (true)

	
	}

}


//DvbRec.main(args)
