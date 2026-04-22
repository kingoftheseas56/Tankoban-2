# UIA enumeration helper for Tankoban recon.
# Usage: .\uia-dump.ps1 [-MaxDepth 6] [-TargetClass StreamPage]
#
# Queries Tankoban's top-level window, enumerates the UIA tree, prints one line
# per element with [ControlType] Name | AutomationId | ClassName.
#
# Reusable by any agent doing UIA recon. Requires Tankoban running.

param(
    [int]$MaxDepth = 6,
    [string]$TargetClass = ""
)

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$automation = [System.Windows.Automation.AutomationElement]::RootElement
$tankoban = $automation.FindFirst(
    [System.Windows.Automation.TreeScope]::Children,
    (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, "Tankoban"))
)

if (-not $tankoban) {
    Write-Error "Tankoban window not found. Is Tankoban.exe running?"
    exit 1
}

function Get-UIATree {
    param(
        [System.Windows.Automation.AutomationElement]$Element,
        [int]$Depth = 0,
        [int]$MaxDepth = 6
    )
    if ($Depth -gt $MaxDepth) { return }
    $indent = "  " * $Depth
    $nameRaw = $Element.Current.Name
    if ($nameRaw.Length -gt 50) { $nameRaw = $nameRaw.Substring(0,50) + ".." }
    $nameRaw = $nameRaw -replace "`n"," "
    $automationId = $Element.Current.AutomationId
    if ($automationId.Length -gt 90) { $automationId = "..." + $automationId.Substring($automationId.Length - 87) }
    $controlType = $Element.Current.ControlType.ProgrammaticName -replace 'ControlType\.', ''
    $className = $Element.Current.ClassName
    Write-Output ("{0}[{1,-10}] '{2}' | {3} | class={4}" -f $indent, $controlType, $nameRaw, $automationId, $className)
    try {
        $children = $Element.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
        foreach ($child in $children) { Get-UIATree -Element $child -Depth ($Depth + 1) -MaxDepth $MaxDepth }
    } catch {}
}

if ($TargetClass -ne "") {
    $target = $tankoban.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants,
        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty, $TargetClass))
    )
    if (-not $target) { Write-Error "Class '$TargetClass' not found in descendants."; exit 2 }
    Get-UIATree -Element $target -MaxDepth $MaxDepth
} else {
    Get-UIATree -Element $tankoban -MaxDepth $MaxDepth
}
