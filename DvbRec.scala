
import java.util.Properties
import java.io.FileInputStream
import scala.actors.Actor
import Actor._
import java.io._
import java.util.Calendar
import java.util.Calendar._

class Runner {

	def run(command:String):Process = {
                val args = command.split(" ")
                val processBuilder = new ProcessBuilder(args: _* )
                processBuilder.redirectErrorStream(true)
                val proc = processBuilder.start()

                new OutputReader(proc).start()


                return proc
        }

}


class FileMover(val source:String, val target:String) extends Actor {
	
	

	def act() {
		
		System.out.println("Moving %s to %s".format(source,target))

		try {

			val cmd = "cmd /c move %s %s".format(source,target)// .split("\\s")
			
			val transproc =  new Runner().run(cmd)  //new ProcessBuilder( java.util.Arrays.asList(cmd: _*)).start()

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
                                        
				val newline:String = line
								
				
				System.out.println("[%s] %s".format(Thread.currentThread,line))
				
                }
                bufferedReader.close
                        

        
        
        }

}

object DvbRec {

        import java.io._
        import scala.actors._
        import scala.actors.Actor._
  
	// SETTINGS
	val TMP_ENCODING_PATH = "C:\\"; 
	val FFMPEG_PATH = "c:\\ffmpeg-20120608\\bin\\ffmpeg.exe"; 
	val MENCODER_PATH = "c:\\mencoder\\mencoder.exe";

        


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

		
		
		val OPT_TUNER_ID = props.getProperty("TUNERID")
		val OPT_CHANNEL_NAME = props.getProperty("CHANNELNAME")
		val OPT_RECPATH = props.getProperty("RECPATH")
		val OPT_FLV_ENABLED = props.getProperty("FLV_ENABLED")
		val OPT_FLV_RECPATH = props.getProperty("FLV_RECPATH")	
		val OPT_LOOKUP_PATH = props.getProperty("LOOKUP_PATH");
		
		new File(OPT_RECPATH).mkdirs
		new File(OPT_FLV_RECPATH).mkdirs
		
		System.out.println("Launching %s transcoding...".format(OPT_CHANNEL_NAME))
                

		val fileMonitor = new FileMonitor(OPT_TUNER_ID.toInt,
										OPT_LOOKUP_PATH) 

		
		

	

		do {
			var waiting_to_transcode:List[String] = fileMonitor.check
			
                  if (waiting_to_transcode.length > 0) {
                        
                        val file_to_transcode = waiting_to_transcode(0)
                      //  fileMonitor.waiting_to_transcode = fileMonitor.waiting_to_transcode.filterNot(p => { p == file_to_transcode } )

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

			
		        val tmp_mpg_loc = "%s\\dvbrec.%s.%s.mpg".format(TMP_ENCODING_PATH,
												    OPT_TUNER_ID,System.nanoTime)
		        val tmp_flv_loc = "%s\\dvbrec.%s.%s.flv".format(TMP_ENCODING_PATH,
												    OPT_TUNER_ID,System.nanoTime)

			val fixed_file_to_transcode_path = "%s.fixed".format(file_to_transcode)

			val CMD_REINDEX_TS = "%s -idx %s -oac copy -ovc copy -o %s".format(MENCODER_PATH,file_to_transcode, fixed_file_to_transcode_path)

			val CMD_REINDEXER = new Runner().run(CMD_REINDEX_TS);

			CMD_REINDEXER.waitFor()

			new Runner().run("cmd /c del "+file_to_transcode).waitFor()

			new Runner().run("cmd /c move "+fixed_file_to_transcode_path+" "+file_to_transcode).waitFor()


			val CMD_MPEG_TRANSCODE = "%s -i %s -threads 2 -target pal-vcd -async 44100 -y %s".format(FFMPEG_PATH,file_to_transcode,tmp_mpg_loc)
			System.out.println(CMD_MPEG_TRANSCODE)
                        val MPEG_TRANSCODER = new Runner().run(CMD_MPEG_TRANSCODE)

			MPEG_TRANSCODER.waitFor()

			
			
			var FLV_TRANSCODER: Process  = null
			if (encode_flv) {
				val CMD_FLV_TRANSCODE = "%s -i %s -threads 2 -y -acodec libmp3lame -ar 44100 -ab 160k -coder ac -sc_threshold 40 -vcodec libx264 -b 270k -minrate 270k -maxrate 270k -bufsize 2700k -cmp +chroma -partitions +parti4x4+partp8x8+partb8x8 -i_qfactor 0.71 -keyint_min 25 -b_strategy 1 -g 250 -s 352x288 %s"
				.format(FFMPEG_PATH,tmp_mpg_loc,tmp_flv_loc)
			  System.out.println(CMD_FLV_TRANSCODE)
				FLV_TRANSCODER = new Runner().run(CMD_FLV_TRANSCODE)
			}
			
			
                        
                        if (encode_flv) {
				FLV_TRANSCODER.waitFor()

                        }
                        
				new FileMover(tmp_mpg_loc,
				"%s\\CH%s%s.mpg".format(OPT_RECPATH,OPT_TUNER_ID,file_name_root)).start()
			
                      
                        //System.out.System.out.println("Exit value: %s".format(FLV_TRANSCODER.transproc.exitValue))
                        //new File(file_to_transcode).delete()
                  new Runner().run("cmd /c del "+file_to_transcode).waitFor()
                        
			if (encode_flv) {
				(new Actor {
					def act() {
						try {
						import org.red5.io.flv.impl._
						import org.red5.io.flv.meta._
						import org.red5.io._

						val newDirs = "\\%s\\%02d\\%02d\\".format(start_time.get(Calendar.YEAR),
			                        					start_time.get(Calendar.MONTH)+1,
		           		        					start_time.get(Calendar.DAY_OF_MONTH))
						val targetPath = "%s\\%s".format(OPT_FLV_RECPATH, newDirs)

		                        new File(targetPath).mkdirs
											
						

						val newPath = "%s\\%s%s.flv".format(targetPath,
											OPT_CHANNEL_NAME,
											file_name_root);
						 new FileMover(tmp_flv_loc,newPath ).act

						val tFlvFile = new java.io.File(newPath)	
						System.out.println("Generating metadatas for %s".format(newPath))
						val flvReader = new FLVReader(tFlvFile,true);
			                        val metaCache = new FileKeyFrameMetaCache();
	                                	metaCache.saveKeyFrameMeta(tFlvFile, flvReader.analyzeKeyFrames());
						flvReader.close()
						System.out.println("Done!!")
						} catch  {

							case e: Exception => {}
						}
                               			

		                                


					}
					}).act()
			}
	
  


                  } else {
                        Thread.sleep(1000)
                  }
                        
		} while (true)
	
	}

}


//DvbRec.main(args)
