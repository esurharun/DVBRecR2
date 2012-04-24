import scala.actors.Actor
import Actor._
import java.lang.ProcessBuilder
import java.lang.Process
import java.io.InputStreamReader
import java.io.BufferedReader
import java.nio._
import java.nio.channels._

case class StopMessage()

class Transcoder(val bootCommand: String,val tag:String) extends Actor {


        var transproc: Process = null

	var terminate = false;

	var startCalendar: java.util.Calendar  = null 

	def act() {
		val cmd = bootCommand.split("\\s")
	        transproc = new ProcessBuilder( java.util.Arrays.asList(cmd: _*) ).start()
                Console.println(bootCommand)

		var transproc_stderr: BufferedReader =  new BufferedReader(new InputStreamReader(transproc.getErrorStream))
		var transproc_stdout: BufferedReader =  new BufferedReader(new InputStreamReader(transproc.getInputStream))


		def processOutput(out:String)  {

                        if (out == null)
                          return;

                        if (out.indexOf("video:") == 0) {
                                terminate = true
                        }
			
                        if (out.indexOf("frame=") == -1) {
				println("[%s] %s".format(tag, out));
			} else {
				if (startCalendar == null)
					startCalendar = java.util.Calendar.getInstance()
			}
		} 

		loopWhile (terminate == false) {
			Thread.sleep(5)
				processOutput(transproc_stderr.readLine())
				processOutput(transproc_stdout.readLine())
			
	
		}

	}



}
