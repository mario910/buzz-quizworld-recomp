# Press buzzer buttons in a game started with BUZZ_TEST_INPUT_FILE=<mailbox>
# (no window focus needed). Each argument is P<player><button>, button one of
# r(ed) y(ellow) g(reen) o(range) b(lue); several in one argument with '+'
# press together:  claude-press.ps1 -Mailbox m.txt P1r P2r+P3r -Gap 1.5
param([Parameter(Mandatory)][string]$Mailbox, [double]$Gap = 1.0,
      [Parameter(ValueFromRemainingArguments)][string[]]$Presses)
$ErrorActionPreference = 'Stop'
$bit = @{ r = 0; y = 1; g = 2; o = 3; b = 4 }
$text = if (Test-Path -LiteralPath $Mailbox) { Get-Content -LiteralPath $Mailbox -Raw } else { '' }
$seq = if ($text -match '^(\d+):') { [uint64]$Matches[1] } else { 0 }
foreach ($press in $Presses) {
    $mask = 0
    foreach ($p in $press -split '\+') {
        if ($p -notmatch '^[Pp]([1-4])([rygob])$') { throw "bad press '$p' (want P<1-4><r|y|g|o|b>)" }
        $mask = $mask -bor (1 -shl (([int]$Matches[1] - 1) * 5 + $bit[$Matches[2]]))
    }
    $seq++
    [IO.File]::WriteAllText($Mailbox, ("{0}:{1:x}`n" -f $seq, $mask), [Text.UTF8Encoding]::new($false))
    Start-Sleep -Milliseconds ([int]($Gap * 1000))
}
