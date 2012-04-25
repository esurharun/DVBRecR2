
import java.util.Properties
import java.io.FileInputStream
import scala.actors.Actor
import Actor._
import java.io._
import java.util.Calendar
import java.util.Calendar._
class FileMover(val source:String, val target:String) extends Actor {
	
	def act() {
		
		System.out.println("Moving %s to %s".format(source,target))

		try {

			val cmd = "mv %s %s".format(source,target) .split("\\s")
			val transproc = new ProcessBuilder( java.util.Arrays.asList(cmd: _*)).start()

			transproc.waitFor()


			System.out.println("Successfully moved %s to %s".format(source,target))
			//sFile.delete
		} catch {
			case e: Exception => {
				System.out.println("Could not copied %s to %s".format(source,target))
			}
		}
	}

}


class OutputReader(val proc:Process) extends Actor {


        def act() {
        
                val streamReader = new java.io.InputStreamReader(proc.getInputStream)
                val bufferedReader = new java.io.BufferedReader(streamReader)
                var line:String = null
                while ({line = bufferedReader.readLine; line != null}) {
                                        
                         System.out.println("[%s] %s".format(Thread.currentThread,line))
                }
                bufferedReader.close
                        

        
        
        }

}

object DvbRec {

        import java.io._
        import scala.actors._
        import scala.actors.Actor._

        def run(command:String):Process = {
                val args = command.split(" ")
                val processBuilder = new ProcessBuilder(args: _* )
                processBuilder.redirectErrorStream(true)
                val proc = processBuilder.start()

                new OutputReader(proc).start()


                return proc
        }


	def main(args: Array[String]): Unit = {

		if (args.length == 0) {
			System.out.println("You should give a configuration file")
			exit
		}

		var props = new Properties()

		try {
			props.load(new FileInputStream(args(0)))
		} catch  {

			case e: Exception => {
				System.out.println("Cannot read: %s".format(args(0)))
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


                val cur_time = java.util.Calendar.getInstance()
					
                val hour = cur_time.get(Calendar.HOUR_OF_DAY)
		val min = cur_time.get(Calendar.MINUTE)
		val sec = cur_time.get(Calendar.SECOND)


                val next_stop_hour = if (hour % 2 == 0) { hour+2 } else { hour+1 }
                val next_stop_min = (next_stop_hour-hour)*60
                val first_interval_sec = (next_stop_min*60)-((min*60)+sec) 

		val dvbMonitor = new DvbMonitor(OPT_CHANNEL_ID.toInt,
										OPT_FREQUENCY,
										OPT_POL,
										OPT_SYMBOLRATE,
										OPT_DISEQC,
										OPT_AUDIOPID.toInt,
										OPT_VIDEOPID.toInt,
                          //      200,200)
                                                                                first_interval_sec,
                                                                                7200) 

		dvbMonitor.start()
		System.out.println("Locked!!!")

	

		do {

                  if (dvbMonitor.waiting_to_transcode.length > 0) {
                        
                        val file_to_transcode = dvbMonitor.waiting_to_transcode(0)
                        dvbMonitor.waiting_to_transcode = dvbMonitor.waiting_to_transcode.filterNot(p => { p == file_to_transcode } )

                        val time_range_pat = """(\d+)\-(\d+)""".r
                        val time_millis =  time_range_pat.findAllIn(file_to_transcode).toSeq(0).split("-")
                        val st_time_millis = time_millis(0).toLong
                        val en_time_millis = time_millis(1).toLong
                  
                        val start_time = java.util.Calendar.getInstance() 
                        start_time.setTimeInMillis(st_time_millis)

                        val end_time = java.util.Calendar.getInstance()
                        end_time.setTimeInMillis(en_time_millis)

                        val file_name_root  = "-%s%02d%02d_%02d%02d%02d-%02d%02d%02d".format(
									start_time.get(Calendar.YEAR),
									start_time.get(Calendar.MONTH)+1,
									start_time.get(Calendar.DAY_OF_MONTH),
									start_time.get(Calendar.HOUR_OF_DAY),
									start_time.get(Calendar.MINUTE),
									start_time.get(Calendar.SECOND),
									end_time.get(Calendar.HOUR_OF_DAY),
									end_time.get(Calendar.MINUTE),
									end_time.get(Calendar.SECOND)
									)


                        val encode_flv = OPT_FLV_ENABLED.trim.toLowerCase == "true"

			
		        val tmp_mpg_loc = "/tmp/dvbrec.%s.%s.mpg".format(OPT_CHANNEL_ID,System.nanoTime)
		        val tmp_flv_loc = "/tmp/dvbrec.%s.%s.flv".format(OPT_CHANNEL_ID,System.nanoTime)

                        val MPEG_TRANSCODER = run("ffmpeg -i %s -target pal-vcd -async 44100 -y %s".format(file_to_transcode,tmp_mpg_loc))
			var FLV_TRANSCODER: Process  = null
			if (encode_flv) {
				FLV_TRANSCODER = run("ffmpeg -i %s -y -acodec libfaac -ar 22500 -ab 96k -coder ac -sc_threshold 40 -vcodec libx264 -b 270k -minrate 270k -maxrate 270k -bufsize 2700k -cmp +chroma -partitions +parti4x4+partp8x8+partb8x8 -i_qfactor 0.71 -keyint_min 25 -b_strategy 1 -g 250 -s 352x288 %s".format(file_to_transcode,tmp_flv_loc))
			}
			
			MPEG_TRANSCODER.waitFor()
                        
                        if (encode_flv) {
				FLV_TRANSCODER.waitFor()

                        }
                        
			new FileMover(tmp_mpg_loc,
				"%s/CH%s%s.mpg".format(OPT_RECPATH,OPT_CHANNEL_ID,file_name_root)).start()
                      
                        //System.out.System.out.println("Exit value: %s".format(FLV_TRANSCODER.transproc.exitValue))
                        new File(file_to_transcode).delete()



			if (encode_flv) {
				(new Actor {
					def act() {
						import org.red5.io.flv.impl._
						import org.red5.io.flv.meta._
						import org.red5.io._
											
						val tFlvFile = new java.io.File(tmp_flv_loc)	

						System.out.println("Generating metadatas for %s".format(tmp_flv_loc))
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
											file_name_root)).start
						new FileMover("%s.meta".format(tmp_flv_loc),
									  "%s/%s%s.flv.meta".format(targetPath,
						        			  	OPT_CHANNEL_NAME,
						        				  file_name_root)).start
					}
					}).start()
			}
	
  


                  } else {
                        Thread.sleep(1000)
                  }
                        
		} while (true)
	
	}

}


//DvbRec.main(args)
