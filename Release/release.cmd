del BloodlineFix.zip
for /r %%x in (*.dll) do 7za a -tzip "BloodlineFix.zip" %%x
for /r %%x in (*.ini) do 7za a -tzip "BloodlineFix.zip" %%x
for /r %%x in (*.pdb) do 7za a -tzip "BloodlineFix.zip" %%x
