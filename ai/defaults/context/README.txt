COURSE REFERENCE MATERIAL
==========================

Put reference files for your course in this folder (or in a folder named
.ai-context next to your model files):

  - Text files: .txt, .md
  - MathProg and data files: .mod, .dat, .lp, .mps
  - Data tables: .csv

HOW THE ASSISTANT USES THESE FILES

On every question, the assistant checks the files in this folder. If all files
together fit within the context budget (12,000 characters by default), all of
them are provided to the model.

If the files exceed the budget, the assistant automatically ranks them by how
many words (of 4 or more letters) they share with your question, and includes
the most relevant excerpts first.

TIPS FOR BEST RESULTS

  - Use one topic or exercise per file with a clear descriptive name
    (e.g., transportation_problem.mod, diet_problem_notes.txt, branch_and_bound.md).
  - Include example formulations, standard notation, parameter naming
    conventions, and problem descriptions from your course slides or notes.
  - The assistant will match the notation and style of your instructor's examples.
