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

#with open("6react_no_commute_result.txt", "w") as rto:
command = "/usr/bin/time -o 6react_no_commute_ragtimer_time.txt python3 ragtimer.py loose qty 100 > 6react_no_commute_result.txt"
print("RAGTIMER is generating traces")
os.system(command)
#    subprocess.run(command, stdout=rto, stderr=subprocess.DEVNULL)


# run prism (and get time)
#command = "/usr/bin/time -o 2react_no_commute_prism_time.txt prism -importmodel _commute/prism.tra,sta,lab -ctmc model.csl > 2react_no_commute_prism_output.txt"
print("Probability is being calculated")
#os.system(command)
print("Testing is complete for 6react without commuting.\n\nThe lower-bound probability is found in '6react_no_commute_result.txt' around line 138, the time RAGTIMER needed to run is found in '6react_no_commute_ragtimer_time.txt'")


