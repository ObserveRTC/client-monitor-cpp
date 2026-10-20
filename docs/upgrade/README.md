# Upgrade steps

`X.Y.Z.md` holds the steps an application takes to move to version X.Y.Z: the API it has to
change, the integration it has to check, and how to verify it. The Release workflow puts it into
`upgrade-prompt.md` (from `docs/upgrade-prompt.md`), which is attached to the GitHub release so it
can be handed to a coding agent as it is.

A release whose CHANGELOG section says **Breaking** must have its file here; the release is refused
without it. For other releases it is optional.

Write the steps for whoever maintains an application: imperative, concrete, with the names to
search for. Keep them about the library; the application is not known here.
