# Project Tracking Rule

When working in this workspace, ALWAYS adhere to the following workflow:

1. **Initialization**: At the start of any task or session, read `docs/feature_implementation_plan.md` to understand the current project status and the overall plan.
2. **Completion**: Before finishing a task or ending the session, ALWAYS update `docs/feature_implementation_plan.md`. You must document:
   - The progress made during the current session.
   - The upcoming tasks or next steps to be implemented.
3. **ESP-IDF Environment**: When executing `idf.py` commands (such as `idf.py build`, `idf.py flash`, `idf.py monitor`), ALWAYS run the `get_idf` alias first to load the ESP-IDF environment (e.g., using `zsh -i -c "get_idf && idf.py build"`).
