"""taskprocessor_overload_trigger

Revision ID: f3c0b8695b66
Revises: 0838f8db6a61
Create Date: 2019-02-15 15:03:50.106790

"""

# revision identifiers, used by Alembic.
revision = 'f3c0b8695b66'
down_revision = '0838f8db6a61'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

PJSIP_TASKPROCESSOR_OVERLOAD_TRIGGER_NAME = 'pjsip_taskprocessor_overload_trigger_values'
PJSIP_TASKPROCESSOR_OVERLOAD_TRIGGER_VALUES = ['none', 'global', 'pjsip_only']

def upgrade():
    context = op.get_context()

    if context.bind.dialect.name == 'postgresql':
        enum = ENUM(*PJSIP_TASKPROCESSOR_OVERLOAD_TRIGGER_VALUES,
                    name=PJSIP_TASKPROCESSOR_OVERLOAD_TRIGGER_NAME)
        enum.create(op.get_bind(), checkfirst=False)

    op.add_column('ps_globals',
        sa.Column('taskprocessor_overload_trigger',
            sa.Enum(*PJSIP_TASKPROCESSOR_OVERLOAD_TRIGGER_VALUES,
            name=PJSIP_TASKPROCESSOR_OVERLOAD_TRIGGER_NAME)))

def downgrade():
    context = op.get_context()

    if context.bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_globals_taskprocessor_overload_trigger_pjsip_taskprocessor_overload_trigger_values', 'ps_globals')
    op.drop_column('ps_globals', 'taskprocessor_overload_trigger')

    if context.bind.dialect.name == 'postgresql':
        enum = ENUM(*PJSIP_TASKPROCESSOR_OVERLOAD_TRIGGER_VALUES,
                    name=PJSIP_TASKPROCESSOR_OVERLOAD_TRIGGER_NAME)
        enum.drop(op.get_bind(), checkfirst=False)
