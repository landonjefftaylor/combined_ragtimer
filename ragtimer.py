import os
import sys
import subprocess

if __name__ == "__main__":
    y = "y"
    print("Welcome to RAGTIMER. At present, this is two tools joined together. We are working on joining them together more seamlessly but appreciate your patience with this prototype tool.")
    wantCommute = False
    if len(sys.argv) == 1:
        wantCommute = y in input("Would you like to commute? y/n ").lower()
        print("I will generate traces, then commute. Next time, you can use the option --commute from the command line.")
    else:
        wantCommute = "--commute" in sys.argv
        print("I will generate traces, then commute.")
    print("I am assuming you have placed your model in the following files:")
    print("model.ragtimer (in the RAGTIMER format; see README)")
    print("model.prop (file with property without time constraints in first line, i.e., x=10 )")
    print("model.sm (prism model file)")
    print("model.csl (prism csl property)")
    print()

    if wantCommute:
        with open("_commute/options.txt", "w") as options:
            print(options.name)
            options.write("model ../model.sm")
            options.write("\n")
            options.write("trace ../commute_traces.txt")
            options.write("\n")
            property = ""
            with open("model.prop", "r") as prop:
                for line in prop:
                    property = line.strip()
                    break
            options.write("property " + property)
            options.write("\n")
            print("I will now ask a few questions about the commuting options before beginning trace generation:\n")
            bound = input("Would you prefer to use a time bound (t), recursion bound (r), or both (b)? t/r/b ").lower()
            if "b" in bound:
                options.write("recursionBound " + input("What is your max recursion bound (20 is normal)? "))
                options.write("\n")
                options.write("timeBound " + input("What is your model property time bound? "))
                options.write("\n")
                options.write("flexibility " + input("What time bound flexbility (0.1-0.3 gives good results)? "))
                options.write("\n")
                options.write("terminate both")
                options.write("\n")
            elif "t" in bound:
                options.write("recursionBound 100000")
                options.write("\n")
                options.write("timeBound " + input("What is your model property time bound? "))
                options.write("\n")
                options.write("flexibility " + input("What time bound flexbility (0.1-0.3 gives good results)? "))
                options.write("\n")
                options.write("terminate time")
                options.write("\n")
            else:
                options.write("recursionBound " + input("What is your max recursion bound (20 is normal)? "))
                options.write("\n")
                options.write("timeBound 100000")
                options.write("\n")
                options.write("flexbility 0.0")
                options.write("\n")
                options.write("terminate depth")
                options.write("\n")
            numCycles = int(input("What max cycle length would you like (0 for no cycles)? "))
            options.write("cycleLength " + str(numCycles))
            options.write("\n")
            options.write("export " + input("Would you like to export to prism, storm, or both? prism/storm/both "))
            options.write("\n")
            options.write("verbose false")
            # options.write("verbose false")
            options.write("\n")


    os.chdir("_ragtimer")
    loose = y in input("Would you like to add the loose command to RAGTIMER? y/n ").lower()
    each = y in input("Would you like to add the each command to RAGTIMER? y/n ").lower()
    qty = int(input("How many traces to generate (type an integer) "))
    command = "python3 main.py " + str(qty)
    command += " loose" if loose else ""
    command += " each" if each else ""
    command += " commute" if wantCommute else ""
    input("Click enter to run the command " + command + ", or CTRL+C to terminate without running")
    print("Running RAGTIMER trace generation")
    try:
        # os.system(command)
        subprocess.call(command.split())
    except:
        print("If you see a lot of errors here, you may need to edit the Makefile in directory _ragtimer to point to your accurate PRISM directory.")

    if wantCommute:
        print("Starting Commuting")
        os.chdir("../_commute")
        try:
            subprocess.call(["make"])
            subprocess.call(["make", "test"])
            # os.system("make")
            # os.system("make test")
        except:
            print("If you see a lot of errors here, you may need to edit the Makefile in directory _commute to point to your accurate PRISM directory.")

        print("Look for output files _commute/prism.* or _commute/storm.*")
        print("You can use these files in your favorite model checker.")


#TODO: Include options to run prism/storm directly from here
#TODO: Not sure how to make a time bound work