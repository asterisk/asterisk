"""fix suppress_q850_reason_headers

Revision ID: 7f85dd44c775
Revises: fe6592859b85
Create Date: 2018-09-18 16:16:29.304815

"""

# revision identifiers, used by Alembic.
revision = '7f85dd44c775'
down_revision = 'fe6592859b85'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']


def upgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    if op.get_context().bind.dialect.name != 'mssql':
        op.alter_column('ps_endpoints', 'suppress_q850_reason_header', type_=yesno_values,
                        new_column_name='suppress_q850_reason_headers')
    else:
        op.alter_column('ps_endpoints', 'suppress_q850_reason_header', existing_type=yesno_values, type_=sa.String(3),
                        new_column_name='suppress_q850_reason_headers')
        yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=True)
        op.alter_column('ps_endpoints', 'suppress_q850_reason_headers', existing_type=sa.String(3), type_=yesno_values)


def downgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
    if op.get_context().bind.dialect.name != 'mssql':
        op.alter_column('ps_endpoints', 'suppress_q850_reason_headers', type_=yesno_values,
                        new_column_name='suppress_q850_reason_header')
    else:
        op.alter_column('ps_endpoints', 'suppress_q850_reason_headers', existing_type=yesno_values, type_=sa.String(3),
                        new_column_name='suppress_q850_reason_header')
        yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=True)
        op.alter_column('ps_endpoints', 'suppress_q850_reason_header', existing_type=sa.String(3), type_=yesno_values)