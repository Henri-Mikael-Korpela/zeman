# Instruction manual

How to use Zeman, on the desktop and on the phone. To build and start the apps, see [Getting started](../README.md#getting-started) in the README.

## Contents

- [The views](#the-views)
- [Connecting a phone](#connecting-a-phone)
- [Using the phone app](#using-the-phone-app)
- [Calendar items](#calendar-items)
  - [Selecting and adding](#selecting-and-adding)
  - [Repeating events](#repeating-events)
  - [Tasks](#tasks)
  - [Locations and descriptions](#locations-and-descriptions)
  - [Problems](#problems)
  - [Editing and deleting](#editing-and-deleting)
  - [When an item was created](#when-an-item-was-created)
- [Calendar views and navigation](#calendar-views-and-navigation)
- [Changing times by dragging and drawing](#changing-times-by-dragging-and-drawing)
- [Keyboard shortcuts](#keyboard-shortcuts)
- [Where your data is kept](#where-your-data-is-kept)

## The views

The navigation on the left of the desktop app switches between **Calendar**, **Problems**, **Tasks** and **Connected Devices**.

- **Calendar** shows your events, tasks and problems by month, week or day, with the selected day's items in a panel on the right.
- **Problems** shows every problem in four columns, from the least to the most complete: No planned tasks, Planned, Partly completed and Completed, each with how many problems it holds.
- **Tasks** shows every task in four columns: Overdue, Today, Upcoming and Completed. A repeating task is shown once, at the occurrence that covers today or the next one, so a missed repeat gives way to the next. Click a task's checkbox to mark it completed, or anywhere else on it to open its day in the calendar.
- **Connected Devices** shows the QR code that phones scan to connect, the address to type instead, and the phones connected now.

## Connecting a phone

The phone and the desktop talk directly, within the same local network; nothing goes through the internet.

1. Start the desktop app and open **Connected Devices** in its navigation. The first time, Windows asks whether to allow it through the firewall: allow it on private networks.
2. Open the app on the phone, on the same Wi-Fi network, and press **Scan QR code** (recommended). Allow the camera when Android asks, and point it at the code on the desktop's screen.

Instead of scanning, type the address shown under the code (also in the desktop's status bar) into the phone. When the phone finds a desktop app on the network by itself, it also offers it with a **Use it** button.

The phone remembers the desktop and connects to it at the next start. To change or reconnect to it, open the navigation from the left edge of the screen (tap the arrow on the edge, or drag it out) and choose **Connection settings**, which offer the QR code, a desktop found on the network and the typed address. Changes made on either side appear on the other within about a second.

The desktop app accepts connections only from local network addresses (10.x.x.x, 172.16-31.x.x, 192.168.x.x and 169.254.x.x) and has no password, so anyone on the same local network can use it while it runs.

`zeman.exe --port PORT` serves phones on another port than 47800, for example when something else uses it; the QR code and the address shown carry the port. `zeman.exe --connect ADDRESS[:PORT]` runs the desktop app as a client of another desktop app, exactly as the phone does; it is a way to try the network side on one computer.

## Using the phone app

- Swipe sideways over the calendar to move to the previous or next month, week or day.
- Back closes a form, and closes the navigation when it is open.
- The system's status bar (time, battery) and navigation bar (the three buttons or the gesture strip) keep their own space around the app.
- Typing uses the on-screen keyboard and accepts any character the keyboard types, such as a-umlaut on a Finnish keyboard. Characters that the app's font has no glyph for, such as Chinese or emoji, are stored but may display as blanks.
- Dragging with a finger scrolls the day's panel and the hours.

## Calendar items

### Selecting and adding

Click or tap an item in the calendar, a bar in a day or a block on the hours, to select it: its day opens in the panel, which scrolls to its card and rings it. On the desktop the pointer turns into a hand over items you can click. Select a day to see its events and tasks in the panel on the right, and use **New event**, **New task**, **New problem** or **New opportunity** to add one. When a day has more items than fit, scroll the panel with the mouse wheel, or drag it with a finger or the mouse; the bar at its right shows where you are.

In the month view on the desktop you can also add an item by marking days with the mouse. Press on a day, away from its items, and hold; drag to the last day, and release. The form for a new event opens with those days as its start and end dates (switch it to **Task** in the form if it is one). To reach a day in another month, turn the mouse wheel over the calendar while holding the button. Holding the press for a moment and releasing it on the same day opens the form for that day alone, while a quick click still only selects the day. Escape cancels while marking. Until you save, the new item shows on its days as a see-through ghost of its bar, labelled "New event" or "New task" until it has a title; it follows the dates, times and repeat you type in the form, and disappears if you cancel.

Start and end dates are `YYYY-MM-DD`, typed in, or chosen from a small calendar that the arrow button at the right of each date field opens (arrows for other months; Escape or a click elsewhere closes it). Times are `HH:MM` and optional: leave both empty for an all-day event.

### Repeating events

An event can repeat every week, every X days, every month or every year. Monthly and yearly events keep the same date, so an event on the 31st skips months without one, and an event on 29 February appears only in leap years. Each repetition must end before the next one starts. A repeating event can have an optional **Repeat until** date: it is inclusive, so an occurrence that starts on that date still happens, in full, and none start after it. Leave it empty to repeat forever.

### Tasks

A task works like an event (dates, optional times, repeats, editing and deleting) and can also be checked off: tick its checkbox on its card. For a repeating task, each occurrence is checked off separately. In the calendar, tasks are outlined bars with a checkbox; done tasks are faded and struck through. A click on the check box at the left of a task's bar, as in a day cell or the untimed rows of a week or day, also marks that occurrence done, or not done; the same goes for a task's block on the hours. The Event/Task toggle in the form switches an item between the two.

A task that does not repeat can have an optional **Deadline**: a date, and optionally a time, in the form's last row, where a repeating task has its Repeat until. It cannot be before the task's start date, and the card shows it as "due" after the dates.

### Locations and descriptions

An event or task can have a location: fill in its address, its city, or both. Locations are shared, so every item saved with the same address and city links to the same location, and the card shows it below the dates.

Events and tasks also have an optional one-line description (up to 199 characters). Cards show it under the dates and location, wrapped over up to three lines.

### Problems

Use **New problem** to record a problem: something that went wrong, with a name and an optional description. A problem is always recorded on the day it is created, whichever day is selected, and keeps that day when edited; it has no times, location or repeat. Problems show in dark red, marked with "!", and an event or task cannot be turned into a problem or back.

Tasks can solve problems. A task's form lists every problem below its fields, under **Problems this task solves**, with how many are checked; check the ones that completing the task solves, and save. The list scrolls when it does not fit. Opening a problem then lists its tasks and its status: **No planned tasks**, **Planned** (tasks linked, none done), **Partly completed** or **Completed**. A task counts as completed once any of its occurrences is checked off. The tasks are shown as cards like the day's list, with their dates, location and description; the check box on a card marks the task done or not done from the problem, and the arrow at the right of the card closes the form and shows the day the task starts on. A completed problem turns green, with a check mark, in the calendar and in the day's list.

### Editing and deleting

To edit an item, click its card: the form opens with its details, and **Save** updates it. A repeating event is edited as a whole, so the form shows the dates of its first occurrence. Editing keeps the original creation time.

To delete an item, press **Delete** on its card and confirm; deleting a repeating event removes all of its repeats. Escape cancels the confirmation.

### When an item was created

Each item records when it was created:

- A colored dot in a day cell marks the day an item was created.
- The dot of the selected event or task is ringed, and its card shows when it was created and how long ago, such as `Created 18 Oct 2026, 20:47 (yesterday)`: minutes and hours for the first hours, then "today", "yesterday", days, "last week", weeks, "last month", months, "last year" and years.

## Calendar views and navigation

The **Month**, **Week** and **Day** buttons at the top choose how many days the calendar shows (on a phone, in a row under the header). The arrow buttons, Page Up/Page Down, the mouse wheel over the calendar and sideways swipes on a phone move by that range. **Today** jumps back to the current day.

The month view numbers each week in a narrow column on the left, and the week view's title ends with the week's number, as in "19 - 25 October 2026, week 43". Weeks start on Monday and are numbered as ISO 8601 does, where week 1 is the week with the year's first Thursday, so the last days of December can be in week 1 and the first days of January in week 52 or 53.

In the week and day views, a white line across today's hours marks the time now and moves on every minute; other days have none.

## Changing times by dragging and drawing

These work in the week and day views on the desktop.

**Dragging.** An item on the hours can be changed by dragging: its top or bottom edge (the pointer turns into an up-and-down arrow there) changes its start or end time, and anywhere else on it moves the whole item to another time, or in the week view to another day, keeping its length. The changes snap to quarter hours, or to five minutes with zoom on (press Z); a preview shows the new times while dragging, and the release saves them. A repeating item changes as a whole, as when editing it. Escape cancels a drag. Dragging is off while a form is open, so that the form cannot save old times back over it.

**Moving a task without a time.** In the week view, a task that has no time sits in its day's cell above the hours. Drag it to another day's cell and release to move it there; a ghost bar shows where it will land, and it keeps its length in days. A repeating task moves as a whole. Escape cancels the drag, and a click that does not leave the day still just selects the task. Press on its check box to mark it done instead.

**Drawing.** Drag over a day's hours to draw a block. Before you press, a short line under the pointer shows where the block will start, with its time. The block snaps to quarter hours, and on release the form for a new event opens with that day and the block's start and end times (switch it to **Task** in the form if it is one). Until you save, the new item shows on the timeline as a see-through ghost block, labelled "New event" until it has a title; it follows the times, date and repeat you type in the form, and disappears if you cancel. A block that reaches midnight ends at 23:59. Escape cancels a block while drawing.

**Zoom.** Press Z to toggle drawing in five-minute steps instead: while zoom is on, a round magnifier follows the pointer, with the start line in it, showing every five minutes and the time the edge will get, and the block moves at a quarter of the pointer's speed so that each step is easy to hit. The magnifier also shows the day's existing items at their exact times, and with zoom on you can click, move and drag the edges of items as usual: what the pointer would grab (an edge, or the whole item) is highlighted in the magnifier, and the grab follows the exact times shown there.

The mouse wheel scrolls the hours; on a phone, dragging scrolls them.

## Keyboard shortcuts

| Key | Action |
| --- | --- |
| Arrow keys | Move by a day or a week |
| Page Up / Page Down | Previous or next month, week or day |
| M, W, D | Switch between month, week and day |
| Z | Toggle the five-minute zoom on the hours |
| Home | Jump to today |
| Tab | Move between form fields |
| Escape | Cancel the form, a drag, a drawn block or a confirmation |
| Ctrl+V | Paste at the cursor in a field being typed into (line breaks become spaces) |
| Ctrl+C, Ctrl+X | Copy or cut the whole field, because the text boxes have no selection |

## Where your data is kept

Events, tasks and problems are stored in `zeman.db`, an SQLite database on the desktop computer. In a build made from the repository it sits in the repository root, next to the README, whichever build folder the executable is in, so deleting a build folder does not delete your data. In the release folder (`desktop/release/`) it sits next to `zeman.exe` instead, so the whole folder can be copied elsewhere as one unit. The file is not committed to git, so back it up like any other file you want to keep. The phone keeps no calendar data of its own, only the address of its desktop. Databases from earlier versions are upgraded automatically when opened.
