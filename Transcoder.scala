import scala.actors.Actor
import Actor._
import java.lang.ProcessBuilder
import java.lang.Process
import java.io.InputStreamReader
import java.io.BufferedReader

case class StopMessage()

class Transcoder(val bootCommand: String,val tag:String) extends Actor {

	var transproc: Process	= null
	var terminate = false;
	def fork(block : => Unit): Thread = new Thread { override def run() = block }

	def act() {
		val cmd = bootCommand.split("\\s")
		transproc = new ProcessBuilder( java.util.Arrays.asList(cmd: _*)).start()
		var transproc_stderr: BufferedReader =  new BufferedReader(new InputStreamReader(transproc.getErrorStream))
		var transproc_stdout: BufferedReader =  new BufferedReader(new InputStreamReader(transproc.getInputStream))

		loopWhile (terminate == false) {
			Thread.sleep(5)
			if (transproc.getErrorStream.available > 0) {
				val s = transproc_stderr.readLine()
				//Console.println("[%s] %s".format(tag,s))
				
			}
			if (transproc.getInputStream.available > 0) {
				val s = transproc_stdout.readLine()
				//Console.println("[%s] %s".format(tag,s))
			}
			
	
		}

		transproc.destroy()
	}



}