
 
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
import java.io._

class FileMonitor(var tunerId: Int,
		var lookup_path: String) extends Actor {

	
	var added:List[String] = List[String]()

	var waiting_to_transcode: List[String] =  List[String]()

	def act() {
	
               val filename_pat = """_\d_\d+-\d+.ts""".r 

		while (true) {
	
			
		    val rootDir = new File(lookup_path)
		    val files = rootDir.listFiles();

		    files.foreach{ file => 
		      val file_name = file.getName()
		     // System.out.println("%s checking...in %s".format(file_name,lookup_path))
			  if (filename_pat.findAllIn(file.getName()).size > 0) {
			      
			      
			    val absolute_path = file.getAbsolutePath()
			      if (added.indexOf(absolute_path) == -1) {
			      
				  added = added ++:List(absolute_path);
				  waiting_to_transcode = waiting_to_transcode ++: List(absolute_path)
				
				
				System.out.println("%s added to transcode list".format(file_name))
			      } 
			  }
		    }
		    
		   

		    Thread.sleep(5000)
			    					
				
			 
			

		}

	}



}

