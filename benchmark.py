import subprocess
import os
from datetime import datetime

modelArr = ["2react", "6react", "8react"]
looseArr = ["", "loose"]
qtyArr = ["1", "10", "100"]
cycleArr = ["0", "2", "4"]
recBoundArr = ["2", "10", "20"]

# Not commuting, just trace generation
for model in modelArr:
    for loose in looseArr:
        for qty in qtyArr:

            print()
            print()
            print(80*"*")
            print(80*"*")
            print(80*"*")

            # copy over the models
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
            
            # run ragtimer (incl. getting time)
            command = ["/usr/bin/time", "-o", "ragtimer_time.txt", "python3", "ragtimer.py", loose, "qty", qty, ">", "ragtimer_output.txt"]
            print("running " + " ".join(command))
            subprocess.run(command)

            # copy models and results into results folder
            folder = "results/ragtimer/" + qty + "_" + loose
            os.system("mkdir " + folder)
            os.system("cp model.* " + folder)
            os.system("cp *.txt " + folder)
            os.system("cp *.py " + folder)
            with open(folder + "/timestamp.txt", "w") as f:
                f.write(str(datetime.now()))

            input("works? enter for yes.")


# Not commuting, just trace generation
for model in modelArr:
    for cycle in cycleArr:
        for loose in looseArr:
            for recBound in recBoundArr:
                for qty in qtyArr:
                    
                    print()
                    print()
                    print(80*"*")
                    print(80*"*")
                    print(80*"*")
                    
                    # copy over the models
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
                    
                    # run ragtimer (incl. getting time)
                    command = ["/usr/bin/time", "-o", "ragtimer_time.txt", "python3", "ragtimer.py", "commute", loose, "qty", qty, "bound", "r", "recbound", recBound, "cycle", cycle, ">", "ragtimer_output.txt"]
                    print("running " + " ".join(command))
                    subprocess.run(command)

                    # run prism (and get time)
                    command = "/usr/bin/time -o prism_time.txt prism -importmodel _commute/prism.tra,sta,lab -ctmc model.csl".split()
                    print("running " + " ".join(command))
                    subprocess.run(command)

                    # copy models and results into results folder
                    folder = "results/ragtimer/q" + qty + "_r" + recBound + "_c" + cycle + "_" + loose
                    os.system("mkdir " + folder)
                    os.system("cp model.* " + folder)
                    os.system("cp *.txt " + folder)
                    os.system("cp *.py " + folder)
                    os.system("cp _commute/prism.* " + folder)
                    with open(folder + "/timestamp.txt", "w") as f:
                        f.write(str(datetime.now()))
                    
                    