import subprocess
import os
from datetime import datetime

model = "6react"

print("cp models/" + model + "/" + model + ".* .")
os.system("cp models/" + model + "/" + model + ".* .")
print("mv " + model + ".sm model.sm")
os.system("mv " + model + ".sm model.sm")
print("mv " + model + ".csl model.csl")
os.system("mv " + model + ".csl model.csl")
print("mv " + model + ".prop model.prop")
os.system("mv " + model + ".prop model.prop")
print("mv " + model + ".ragtimer model.ragtimer")
os.system("mv " + model + ".ragtimer model.ragtimer")

with open("ragtimer_output.txt", "w") as rto:
    command = ["/usr/bin/time", "-o", "6react_default_ragtimer_time.txt", "python3", "ragtimer.py", "commute", "qty", "100", "bound", "r", "recbound", "20", "cycle", "2", "1>", "ragtimer_output.txt", "2>", "/dev/null"]
    print("RAGTIMER is generating traces")
    subprocess.run(command, stdout=rto, stderr=subprocess.DEVNULL)


# run prism (and get time)
command = "/usr/bin/time -o 6react_default_prism_time.txt prism -importmodel _commute/prism.tra,sta,lab -ctmc model.csl > 6react_default_prism_output.txt"
print("Probability is being calculated")
os.system(command)
os.system("mv 6react_default_prism_output.txt 6react_default_result.txt")
print("Testing is complete for 6react default.\n\nThe lower-bound probability is found in '6react_default_result.txt' on line 62, the time RAGTIMER needed to run is found in '6react_default_ragtimer_time.txt', the time needed for prism is found in '6react_default_prism_time.txt'")


