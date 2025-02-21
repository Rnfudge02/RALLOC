#Third-party imports
import pandas as pd
import matplotlib.pyplot as plt

#Open data from CSV file
data = pd.read_csv("benchmark_results.csv")

#Plot data in plot
plt.plot(data["Size"], data["r_malloc"], label="r_malloc", marker="o")
plt.plot(data["Size"], data["malloc"], label="malloc", marker="s")

#Set labels and title
plt.xlabel("Allocation Size (Bytes)")
plt.ylabel("Time (Secs)")
plt.title("Memory Allocation Performance Between Malloc & r_malloc")

#Stylization options
plt.legend()
plt.grid()
plt.xscale("log")

#Show the plot and save it to disk
plt.show()
plt.savefig()
