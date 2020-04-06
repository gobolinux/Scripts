#!/bin/sh (source)

# Respawn the script, asking for root privileges
Verify_Superuser_() {
   if [ 0 != "$(id -u)" ]
   then
      Log_Verbose "Running as superuser..."
      # Revalidate password for another 5 mins.
      $sudo_validate
      # Run with superuser's HOME.
      exec $sudo_exec -H env $0 "$@"
   fi
}

Verify_Superuser() {
   Verify_Superuser_ "$@"
}
